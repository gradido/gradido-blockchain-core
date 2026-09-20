#include <gtest/gtest.h>

#include "arnm/arena.h"
#include "proto/maps.h"

#include "memory_limit.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <random>
#include <vector>

/*
 * The map prototypes behind bench_tx_index_map. A benchmark row is only worth reading if the
 * variant it times answers correctly, so:
 * every map variant gives the same key the same id as a std::map reference, finds what it holds
 * and nothing else, also on keys built to collide, and survives a refused allocation.
 *
 * The sets themselves left: they live in arnm/roaring_bitmap.h now, and test_tx_index_roaring
 * holds them against CRoaring on real chain data.
 */

namespace {

using Key = std::array<uint8_t, PROTO_KEY_SIZE>;

Key random_key(std::mt19937_64 &rng) {
  Key key;
  for (size_t word = 0; word < 4; ++word) {
    uint64_t value = rng();
    memcpy(key.data() + word * 8, &value, 8);
  }
  return key;
}

/* ----------------------------------------------------------------------------------- maps */

enum class Kind { Stb, Inline, Narrow, Sorted };

struct MapCase {
  const char *name;
  Kind kind;
  proto_hash_mode hash;
  uint8_t load;
};

const MapCase kMapCases[] = {
    {"stb", Kind::Stb, PROTO_HASH_MIX32, 0},
    {"inline raw8", Kind::Inline, PROTO_HASH_RAW8, 75},
    {"inline mix8", Kind::Inline, PROTO_HASH_MIX8, 75},
    {"inline mix32 lf50", Kind::Inline, PROTO_HASH_MIX32, 50},
    {"inline mix32 lf88", Kind::Inline, PROTO_HASH_MIX32, 88},
    {"narrow raw8", Kind::Narrow, PROTO_HASH_RAW8, 75},
    {"narrow mix8", Kind::Narrow, PROTO_HASH_MIX8, 75},
    {"narrow mix32 lf50", Kind::Narrow, PROTO_HASH_MIX32, 50},
    {"narrow mix32 lf95", Kind::Narrow, PROTO_HASH_MIX32, 95},
    {"sorted", Kind::Sorted, PROTO_HASH_RAW8, 0},
};

/** One map of any kind behind a common face; only the test needs the dispatch. */
struct AnyMap {
  Kind kind;
  map_stb stb{};
  map_lpi lpi{};
  map_lpn lpn{};
  map_sorted sorted{};

  arnm_result open(const MapCase &c, uint32_t initial_capacity, arnm *allocator) {
    kind = c.kind;
    switch (kind) {
    case Kind::Stb:
      return map_stb_init(&stb, 42);
    case Kind::Inline:
      return map_lpi_init(&lpi, initial_capacity, c.load, c.hash, 42, allocator);
    case Kind::Narrow:
      return map_lpn_init(&lpn, initial_capacity, c.load, c.hash, 42, allocator);
    case Kind::Sorted:
      return map_sorted_init(&sorted, initial_capacity, allocator);
    }
    return ARNM_ERROR_INVALID_PARAM;
  }
  arnm_result get_or_insert(const Key &key, uint32_t *id, bool *inserted) {
    switch (kind) {
    case Kind::Stb:
      return map_stb_get_or_insert(&stb, key.data(), id, inserted);
    case Kind::Inline:
      return map_lpi_get_or_insert(&lpi, key.data(), id, inserted);
    case Kind::Narrow:
      return map_lpn_get_or_insert(&lpn, key.data(), id, inserted);
    case Kind::Sorted:
      return map_sorted_get_or_insert(&sorted, key.data(), id, inserted);
    }
    return ARNM_ERROR_INVALID_PARAM;
  }
  bool find(const Key &key, uint32_t *id) const {
    switch (kind) {
    case Kind::Stb:
      return map_stb_find(&stb, key.data(), id);
    case Kind::Inline:
      return map_lpi_find(&lpi, key.data(), id);
    case Kind::Narrow:
      return map_lpn_find(&lpn, key.data(), id);
    case Kind::Sorted:
      return map_sorted_find(&sorted, key.data(), id);
    }
    return false;
  }
  void close() {
    switch (kind) {
    case Kind::Stb:
      map_stb_free(&stb);
      break;
    case Kind::Inline:
      map_lpi_free(&lpi);
      break;
    case Kind::Narrow:
      map_lpn_free(&lpn);
      break;
    case Kind::Sorted:
      map_sorted_free(&sorted);
      break;
    }
  }
};

/** get_or_insert @p sequence, then check ids, hits and misses against a std::map. */
void check_map_against_reference(
    const MapCase &c,
    const std::vector<Key> &sequence,
    const std::vector<Key> &misses,
    uint32_t initial_capacity,
    arnm *allocator
) {
  SCOPED_TRACE(c.name);
  AnyMap map;
  ASSERT_EQ(map.open(c, initial_capacity, allocator), ARNM_SUCCESS);
  std::map<Key, uint32_t> reference;
  for (const Key &key : sequence) {
    uint32_t id = UINT32_MAX;
    bool inserted = false;
    ASSERT_EQ(map.get_or_insert(key, &id, &inserted), ARNM_SUCCESS);
    auto it = reference.find(key);
    if (it == reference.end()) {
      ASSERT_TRUE(inserted);
      ASSERT_EQ(id, reference.size()) << "ids are dense, in order of first sight";
      reference.emplace(key, id);
    } else {
      ASSERT_FALSE(inserted);
      ASSERT_EQ(id, it->second);
    }
  }
  for (const auto &[key, expected] : reference) {
    uint32_t id = UINT32_MAX;
    ASSERT_TRUE(map.find(key, &id));
    ASSERT_EQ(id, expected);
  }
  for (const Key &key : misses) {
    if (reference.count(key)) { continue; }
    ASSERT_FALSE(map.find(key, nullptr));
  }
  map.close();
}

class TxIndexMaps : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(arnm_init_arena(&arena, 64u * 1024u * 1024u), ARNM_SUCCESS);
  }
  void TearDown() override {
    arnm_release(&arena);
  }
  arnm arena{};
};

TEST_F(TxIndexMaps, EveryVariantAgreesWithReference) {
  std::mt19937_64 rng(7);
  std::vector<Key> pool(5000);
  for (Key &key : pool) { key = random_key(rng); }
  std::vector<Key> sequence;
  for (int i = 0; i < 30000; ++i) { sequence.push_back(pool[rng() % pool.size()]); }
  std::vector<Key> misses(2000);
  for (Key &key : misses) { key = random_key(rng); }
  for (const MapCase &c : kMapCases) {
    check_map_against_reference(c, sequence, misses, 0, &arena);
    arnm_reset(&arena);
    // host mode and a table sized up front take other paths through init and grow
    check_map_against_reference(c, sequence, misses, 1, nullptr);
    check_map_against_reference(c, sequence, misses, 8000, nullptr);
  }
}

TEST_F(TxIndexMaps, KeysBuiltToCollideStayCorrect) {
  std::mt19937_64 rng(11);
  for (uint64_t shared_mask : {~0ull, (1ull << 24) - 1u}) {
    std::vector<Key> keys(1500);
    for (Key &key : keys) {
      key = random_key(rng);
      uint64_t first;
      memcpy(&first, key.data(), 8);
      first = (first & ~shared_mask) | (0x0123456789abcdefull & shared_mask);
      memcpy(key.data(), &first, 8);
    }
    std::vector<Key> sequence = keys;
    sequence.insert(sequence.end(), keys.begin(), keys.end());
    std::vector<Key> misses(keys.begin(), keys.begin() + 100);
    for (Key &key : misses) {
      key[31] ^= 0x5a; // same first word as its neighbours, a different key
    }
    for (const MapCase &c : kMapCases) {
      check_map_against_reference(c, sequence, misses, 0, &arena);
      arnm_reset(&arena);
    }
  }
}

TEST_F(TxIndexMaps, RefusedAllocationLeavesMapIntact) {
  std::mt19937_64 rng(13);
  for (const MapCase &c : kMapCases) {
    if (Kind::Stb == c.kind) { continue; } // stb allocates through malloc, nothing to refuse
    SCOPED_TRACE(c.name);
    // room for a few grows and for the narrow map's first key bucket (4096 keys, 131 KiB)
    alignas(8) static uint8_t blob[512u * 1024u];
    arnm small{};
    ASSERT_EQ(arnm_init_arena_borrow(&small, blob, sizeof(blob)), ARNM_SUCCESS);
    AnyMap map;
    ASSERT_EQ(map.open(c, 0, &small), ARNM_SUCCESS);
    std::vector<Key> held;
    arnm_result result = ARNM_SUCCESS;
    for (int i = 0; i < 100000 && ARNM_SUCCESS == result; ++i) {
      Key key = random_key(rng);
      uint32_t id = 0;
      result = map.get_or_insert(key, &id, nullptr);
      if (ARNM_SUCCESS == result) { held.push_back(key); }
    }
    ASSERT_EQ(result, ARNM_ERROR_OUT_OF_MEMORY);
    ASSERT_FALSE(held.empty());
    for (size_t i = 0; i < held.size(); ++i) {
      uint32_t id = UINT32_MAX;
      ASSERT_TRUE(map.find(held[i], &id));
      ASSERT_EQ(id, i);
    }
  }
}

} // namespace
