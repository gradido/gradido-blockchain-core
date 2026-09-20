#include <gtest/gtest.h>

#include "arnm/graded_block_pool.h"
#include "arnm/roaring_bitmap.h"
#include "arnm/roaring_ops.h"
#include "arnm/roaring_query.h"
#include "bench_chain_data.h"
#include "croaring/roaring.h"

#include "memory_limit.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

/*
 * arnm/roaring_bitmap.h against CRoaring's 32 bit roaring_bitmap_t, on the sets the
 * transaction index keeps -- per address balance, signer and other, per transaction type, per
 * foreign coin community -- built from a real chain file and from a synthetic chain large enough
 * for many containers per set. Every set is compared value for value, then the index queries:
 * ranged AND, ANDNOT of the foreign coin set, the three way OR, range counts, pages both ways,
 * select and contains.
 *
 * CRoaring has no ranged AND, so its side intersects whole sets and cuts the range out after;
 * the answer has to be the same either way.
 *
 * Chain file: $GRD_BENCH_CHAIN_DATA, else ../gradido_blockchain/build/tests/data/blk00000001.dat
 * from the working directory. The real chain test is skipped when neither exists.
 */

namespace {

using Values = std::vector<uint32_t>;

Values ToValues(const roaring_bitmap_t *set) {
  Values values(static_cast<size_t>(roaring_bitmap_get_cardinality(set)));
  if (!values.empty()) { roaring_bitmap_to_uint32_array(set, values.data()); }
  return values;
}

Values ToValues(const arnm_roaring_bitmap &set) {
  Values values(set.cardinality);
  if (!values.empty()) {
    EXPECT_EQ(arnm_roaring_page(&set, 0, set.cardinality, false, values.data()), set.cardinality);
  }
  return values;
}

Values InRange(const Values &values, uint32_t min, uint32_t max) {
  if (min > max) { return {}; }
  return {
      std::lower_bound(values.begin(), values.end(), min),
      std::upper_bound(values.begin(), values.end(), max)
  };
}

/** CRoaring's answer to a ranged operation: the whole result, then the range cut out. */
Values Cut(roaring_bitmap_t *owned, uint32_t min, uint32_t max) {
  Values values = InRange(ToValues(owned), min, max);
  roaring_bitmap_free(owned);
  return values;
}

/** The same sets built twice, once per library. */
struct Index {
  explicit Index(size_t set_count) : arnm_sets(set_count), croaring_sets(set_count) {
    arnm_graded_block_pool_options options{};
    EXPECT_EQ(arnm_graded_block_pool_init(&pool, &options, nullptr), ARNM_SUCCESS);
    EXPECT_EQ(arnm_graded_block_pool_init(&scratch, &options, nullptr), ARNM_SUCCESS);
    for (auto &set : arnm_sets) { arnm_roaring_init(&set); }
    for (auto &set : croaring_sets) { set = roaring_bitmap_create(); }
  }
  ~Index() {
    for (auto &set : arnm_sets) { arnm_roaring_free(&set, &pool); }
    for (auto *set : croaring_sets) { roaring_bitmap_free(set); }
    arnm_graded_block_pool_release(&scratch, nullptr);
    arnm_graded_block_pool_release(&pool, nullptr);
  }
  void Add(size_t set, uint64_t tx_nr) {
    ASSERT_LE(tx_nr, UINT32_MAX);
    const auto value = static_cast<uint32_t>(tx_nr);
    ASSERT_EQ(arnm_roaring_add(&arnm_sets[set], value, &pool), ARNM_SUCCESS);
    roaring_bitmap_add(croaring_sets[set], value);
  }
  std::vector<arnm_roaring_bitmap> arnm_sets;
  std::vector<roaring_bitmap_t *> croaring_sets;
  arnm_graded_block_pool pool{};
  arnm_graded_block_pool scratch{};
};

/** Set numbering: three per address, then one per type, then one per foreign coin. */
struct Layout {
  uint32_t addresses = 0;
  size_t Balance(uint32_t address) const {
    return size_t{address} * 3u;
  }
  size_t Signer(uint32_t address) const {
    return size_t{address} * 3u + 1u;
  }
  size_t Other(uint32_t address) const {
    return size_t{address} * 3u + 2u;
  }
  size_t Type(uint32_t type) const {
    return size_t{addresses} * 3u + type;
  }
  size_t Coin(uint32_t coin) const {
    return Type(GRDT_TRANSACTION_COUNT) + coin;
  }
  size_t Count() const {
    return Coin(BENCH_COMMUNITY_MAX);
  }
};

Layout Build(Index **index, const bench_chain_data &data) {
  std::map<std::array<uint8_t, PROTO_KEY_SIZE>, uint32_t> ids;
  for (uint32_t i = 0; i < data.count; ++i) {
    for (uint8_t a = 0; a < data.records[i].address_count; ++a) {
      std::array<uint8_t, PROTO_KEY_SIZE> key{};
      std::memcpy(key.data(), data.records[i].addresses[a].key, PROTO_KEY_SIZE);
      ids.emplace(key, static_cast<uint32_t>(ids.size()));
    }
  }
  Layout layout;
  layout.addresses = static_cast<uint32_t>(ids.size());
  *index = new Index(layout.Count());
  for (uint32_t i = 0; i < data.count; ++i) {
    const bench_tx_record &record = data.records[i];
    for (uint8_t a = 0; a < record.address_count; ++a) {
      std::array<uint8_t, PROTO_KEY_SIZE> key{};
      std::memcpy(key.data(), record.addresses[a].key, PROTO_KEY_SIZE);
      const uint32_t id = ids.at(key);
      const uint8_t roles = record.addresses[a].roles;
      if (roles & BENCH_ADDRESS_BALANCE) { (*index)->Add(layout.Balance(id), record.tx_nr); }
      if (roles & BENCH_ADDRESS_SIGNER) { (*index)->Add(layout.Signer(id), record.tx_nr); }
      if (roles & BENCH_ADDRESS_OTHER) { (*index)->Add(layout.Other(id), record.tx_nr); }
    }
    if (record.type < GRDT_TRANSACTION_COUNT) {
      (*index)->Add(layout.Type(record.type), record.tx_nr);
    }
    for (uint8_t c = 0; c < record.foreign_coin_count; ++c) {
      (*index)->Add(layout.Coin(record.foreign_coin[c]), record.tx_nr);
    }
  }
  return layout;
}

void ExpectSameSets(const Index &index) {
  for (size_t s = 0; s < index.arnm_sets.size(); ++s) {
    const arnm_roaring_bitmap &mine = index.arnm_sets[s];
    const roaring_bitmap_t *theirs = index.croaring_sets[s];
    ASSERT_EQ(mine.cardinality, roaring_bitmap_get_cardinality(theirs)) << "set " << s;
    ASSERT_EQ(ToValues(mine), ToValues(theirs)) << "set " << s;
    if (mine.cardinality) {
      uint32_t value = 0;
      ASSERT_TRUE(arnm_roaring_minimum(&mine, &value));
      ASSERT_EQ(value, roaring_bitmap_minimum(theirs));
      ASSERT_TRUE(arnm_roaring_maximum(&mine, &value));
      ASSERT_EQ(value, roaring_bitmap_maximum(theirs));
    }
  }
}

/** Every index query, for @p samples addresses, with random ranges inside the chain. */
void ExpectSameQueries(
    Index &index, const Layout &layout, const bench_chain_data &data, uint32_t samples
) {
  std::mt19937 random(11);
  const auto first_tx = static_cast<uint32_t>(data.records[0].tx_nr);
  const auto last_tx = static_cast<uint32_t>(data.records[data.count - 1u].tx_nr);
  auto random_tx = [&] {
    return first_tx + static_cast<uint32_t>(random() % (last_tx - first_tx + 1u));
  };

  // the busiest foreign coin, so ANDNOT has something to take away
  size_t coin = layout.Coin(0);
  for (uint32_t c = 0; c < BENCH_COMMUNITY_MAX; ++c) {
    if (index.arnm_sets[layout.Coin(c)].cardinality > index.arnm_sets[coin].cardinality) {
      coin = layout.Coin(c);
    }
  }

  const uint32_t step = std::max(1u, layout.addresses / samples);
  for (uint32_t address = 0; address < layout.addresses; address += step) {
    uint32_t min = random_tx();
    uint32_t max = random_tx();
    if (min > max) { std::swap(min, max); }
    if (address % 5 == 0) { // the whole chain, and ranges that start or end on no transaction
      min = 0;
      max = UINT32_MAX;
    }
    const size_t balance = layout.Balance(address);
    const size_t signer = layout.Signer(address);
    const size_t other = layout.Other(address);
    const size_t type = layout.Type(1u + address % (GRDT_TRANSACTION_COUNT - 1u));

    auto compare = [&](const char *what, arnm_result result, arnm_roaring_bitmap &out,
                       Values expected) {
      ASSERT_EQ(result, ARNM_SUCCESS) << what;
      ASSERT_EQ(ToValues(out), expected)
          << what << " address " << address << " [" << min << ", " << max << "]";
      arnm_roaring_free(&out, &index.scratch);
    };
    arnm_roaring_bitmap out;
    arnm_roaring_init(&out);
    compare(
        "balance AND type",
        arnm_roaring_and(
            &out, &index.arnm_sets[balance], &index.arnm_sets[type], min, max, &index.scratch
        ),
        out,
        Cut(roaring_bitmap_and(index.croaring_sets[balance], index.croaring_sets[type]), min, max)
    );
    compare(
        "balance ANDNOT coin",
        arnm_roaring_andnot(
            &out, &index.arnm_sets[balance], &index.arnm_sets[coin], min, max, &index.scratch
        ),
        out,
        Cut(roaring_bitmap_andnot(index.croaring_sets[balance], index.croaring_sets[coin]), min,
            max)
    );
    compare(
        "type ANDNOT balance",
        arnm_roaring_andnot(
            &out, &index.arnm_sets[type], &index.arnm_sets[balance], min, max, &index.scratch
        ),
        out,
        Cut(roaring_bitmap_andnot(index.croaring_sets[type], index.croaring_sets[balance]), min,
            max)
    );
    arnm_roaring_bitmap first;
    arnm_roaring_init(&first);
    ASSERT_EQ(
        arnm_roaring_or(
            &first, &index.arnm_sets[balance], &index.arnm_sets[signer], min, max, &index.scratch
        ),
        ARNM_SUCCESS
    );
    roaring_bitmap_t *croaring_first =
        roaring_bitmap_or(index.croaring_sets[balance], index.croaring_sets[signer]);
    compare(
        "balance OR signer OR other",
        arnm_roaring_or(&out, &first, &index.arnm_sets[other], min, max, &index.scratch), out,
        Cut(roaring_bitmap_or(croaring_first, index.croaring_sets[other]), min, max)
    );
    // the same union read in place: count and pages, never built
    {
      roaring_bitmap_t *croaring_union =
          roaring_bitmap_or(croaring_first, index.croaring_sets[other]);
      const Values involved = InRange(ToValues(croaring_union), min, max);
      roaring_bitmap_free(croaring_union);
      const arnm_roaring_bitmap *sets[3] = {
          &index.arnm_sets[balance], &index.arnm_sets[signer], &index.arnm_sets[other]
      };
      arnm_roaring_query query{};
      query.any = sets;
      query.any_count = 3;
      query.min = min;
      query.max = max;
      uint64_t count = 0;
      ASSERT_EQ(arnm_roaring_query_cardinality(&query, &count), ARNM_SUCCESS);
      ASSERT_EQ(count, involved.size()) << "involved count, address " << address;
      const auto skip = static_cast<uint32_t>(random() % (involved.size() + 2u));
      for (bool descending : {false, true}) {
        for (uint32_t from : {0u, skip}) {
          Values page(20);
          uint32_t written = 0;
          ASSERT_EQ(
              arnm_roaring_query_page(&query, from, 20, descending, page.data(), &written),
              ARNM_SUCCESS
          );
          page.resize(written);
          Values expected;
          for (size_t i = from; i < involved.size() && expected.size() < 20u; ++i) {
            expected.push_back(descending ? involved[involved.size() - 1u - i] : involved[i]);
          }
          ASSERT_EQ(page, expected) << "involved page, address " << address << " skip " << from
                                    << " descending " << descending;
        }
      }
    }
    roaring_bitmap_free(croaring_first);
    arnm_roaring_free(&first, &index.scratch);
    compare(
        "type copy range",
        arnm_roaring_copy_range(&out, &index.arnm_sets[type], min, max, &index.scratch), out,
        InRange(ToValues(index.croaring_sets[type]), min, max)
    );

    for (size_t set : {balance, signer, type}) {
      const arnm_roaring_bitmap &mine = index.arnm_sets[set];
      const roaring_bitmap_t *theirs = index.croaring_sets[set];
      ASSERT_EQ(
          arnm_roaring_range_cardinality(&mine, min, max),
          roaring_bitmap_range_cardinality_closed(theirs, min, max)
      ) << "set "
        << set << " [" << min << ", " << max << "]";
      const Values all = ToValues(theirs);
      if (all.empty()) { continue; }
      const auto rank = static_cast<uint32_t>(random() % all.size());
      uint32_t value = 0, theirs_value = 0;
      ASSERT_TRUE(arnm_roaring_select(&mine, rank, &value));
      ASSERT_TRUE(roaring_bitmap_select(theirs, rank, &theirs_value));
      ASSERT_EQ(value, theirs_value) << "select " << rank;
      const uint32_t probe = random_tx();
      ASSERT_EQ(arnm_roaring_contains(&mine, probe), roaring_bitmap_contains(theirs, probe))
          << probe;
      // a page of 20 from a random skip, both ways
      const auto skip = static_cast<uint32_t>(random() % (all.size() + 1u));
      for (bool descending : {false, true}) {
        Values page(20);
        const uint32_t written = arnm_roaring_page(&mine, skip, 20, descending, page.data());
        page.resize(written);
        Values expected;
        for (size_t i = skip; i < all.size() && expected.size() < 20u; ++i) {
          expected.push_back(descending ? all[all.size() - 1u - i] : all[i]);
        }
        ASSERT_EQ(page, expected) << "set " << set << " skip " << skip << " descending "
                                  << descending;
      }
    }
  }
  EXPECT_EQ(index.scratch.lent_bytes, 0u) << "every result went back";
}

std::string ChainPath() {
  if (const char *path = std::getenv("GRD_BENCH_CHAIN_DATA")) { return path; }
  return "../gradido_blockchain/build/tests/data/blk00000001.dat";
}

} // namespace

TEST(TxIndexRoaring, RealChainMatchesCRoaring) {
  const std::string path = ChainPath();
  if (!std::ifstream(path).good()) { GTEST_SKIP() << "no chain file at " << path; }
  bench_chain_data data{};
  ASSERT_EQ(bench_chain_data_load(&data, path.c_str(), bench_default_community_uuid), ARNM_SUCCESS);
  ASSERT_GT(data.count, 0u);
  Index *index = nullptr;
  const Layout layout = Build(&index, data);
  ExpectSameSets(*index);
  ExpectSameQueries(*index, layout, data, layout.addresses);
  delete index;
  bench_chain_data_free(&data);
}

TEST(TxIndexRoaring, SyntheticChainOfManyContainersMatchesCRoaring) {
  // a million transactions: 16 containers for the busiest sets, bitmaps among them
  bench_chain_data data{};
  ASSERT_EQ(bench_chain_data_synthetic(&data, 1000000u, 20000u, 0x726f6172ull), ARNM_SUCCESS);
  Index *index = nullptr;
  const Layout layout = Build(&index, data);
  uint32_t bitmaps = 0, containers_max = 0;
  for (const auto &set : index->arnm_sets) {
    containers_max = std::max(containers_max, set.count);
    for (uint32_t c = 0; c < set.count; ++c) {
      bitmaps += set.containers[c].kind == ARNM_ROARING_BITMAP;
    }
  }
  EXPECT_GT(bitmaps, 0u) << "the synthetic chain has to reach bitmap containers";
  EXPECT_GE(containers_max, 16u);
  ExpectSameSets(*index);
  ExpectSameQueries(*index, layout, data, 3000u);
  delete index;
  bench_chain_data_free(&data);
}
