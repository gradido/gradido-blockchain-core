#ifndef GRADIDO_BLOCKCHAIN_CORE_BENCH_PROTO_MAPS_H
#define GRADIDO_BLOCKCHAIN_CORE_BENCH_PROTO_MAPS_H

/*
 * Four ways to turn a 32 byte public key into a dense local id (0, 1, 2, ... in order of first
 * appearance). The id is what the transaction index would store its per address data under, in
 * an arnm_bvec, so every variant answers exactly the same question and only the lookup differs.
 *
 *   B  map_stb     stb_ds hmgeti/hmput, struct key; allocates through a global macro (malloc)
 *   C1 map_lpi     linear probing, key stored in the slot           {key[32], id}  36 bytes
 *   C2 map_lpn     linear probing, narrow hash in the slot          {hash32, id}    8 bytes
 *                  keys in an arnm_bvec at their id
 *   D  map_sorted  no hash: sorted {key[32], id} array, binary search, memmove insert
 *
 * No variant deletes: the index only ever grows, and reset drops the whole map.
 *
 * Result codes are arnm's. A map that could not grow leaves its content untouched and answers
 * the allocator's code.
 */

#include "arnm/bucket_vector.h"
#include "arnm/memory.h"
#include "arnm/result.h"
#include "proto_common.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Marks an empty slot in both linear probing variants. Ids never reach it. */
#define PROTO_MAP_EMPTY_ID UINT32_MAX

/* ---------------------------------------------------------------- C1: key in the slot */

typedef struct map_lpi_slot {
  uint8_t key[PROTO_KEY_SIZE];
  uint32_t id;
} map_lpi_slot;

typedef struct map_lpi {
  map_lpi_slot *slots;
  uint32_t capacity; /**< power of two, 0 before the first insert */
  uint32_t count;
  uint32_t grow_at; /**< count at which the next insert doubles the table */
  uint8_t load_percent;
  proto_hash_mode hash_mode;
  uint64_t seed;
  arnm *allocator;
} map_lpi;

arnm_result map_lpi_init(
    map_lpi *map,
    uint32_t initial_capacity,
    uint8_t load_percent,
    proto_hash_mode hash_mode,
    uint64_t seed,
    arnm *allocator
);
/** @param[out] inserted true when @p key was new; may be NULL. */
arnm_result map_lpi_get_or_insert(map_lpi *map, const uint8_t *key, uint32_t *id, bool *inserted);
bool map_lpi_find(const map_lpi *map, const uint8_t *key, uint32_t *id);
void map_lpi_free(map_lpi *map);
uint64_t map_lpi_bytes(const map_lpi *map);

/* ---------------------------------------------------------------- C2: narrow hash in the slot */

typedef struct map_lpn_slot {
  uint32_t hash32; /**< folded hash; home slot is hash32 & mask, the rest filters memcmp */
  uint32_t id;
} map_lpn_slot;

typedef struct map_lpn {
  map_lpn_slot *slots;
  arnm_bvec keys; /**< element PROTO_KEY_SIZE, index = id */
  uint32_t capacity;
  uint32_t count;
  uint32_t grow_at;
  uint8_t load_percent;
  proto_hash_mode hash_mode;
  uint64_t seed;
  arnm *allocator;
} map_lpn;

arnm_result map_lpn_init(
    map_lpn *map,
    uint32_t initial_capacity,
    uint8_t load_percent,
    proto_hash_mode hash_mode,
    uint64_t seed,
    arnm *allocator
);
arnm_result map_lpn_get_or_insert(map_lpn *map, const uint8_t *key, uint32_t *id, bool *inserted);
bool map_lpn_find(const map_lpn *map, const uint8_t *key, uint32_t *id);
void map_lpn_free(map_lpn *map);
uint64_t map_lpn_bytes(const map_lpn *map);
/** The key stored under @p id; @p id must be below the count. */
static inline const uint8_t *map_lpn_key(const map_lpn *map, uint32_t id) {
  return (const uint8_t *)arnm_bvec_get(&map->keys, id);
}

/* ---------------------------------------------------------------- D: sorted array */

typedef struct map_sorted_entry {
  uint8_t key[PROTO_KEY_SIZE];
  uint32_t id;
} map_sorted_entry;

typedef struct map_sorted {
  map_sorted_entry *entries;
  uint32_t capacity;
  uint32_t count;
  arnm *allocator;
} map_sorted;

arnm_result map_sorted_init(map_sorted *map, uint32_t initial_capacity, arnm *allocator);
arnm_result map_sorted_get_or_insert(
    map_sorted *map, const uint8_t *key, uint32_t *id, bool *inserted
);
bool map_sorted_find(const map_sorted *map, const uint8_t *key, uint32_t *id);
void map_sorted_free(map_sorted *map);
uint64_t map_sorted_bytes(const map_sorted *map);

/* ---------------------------------------------------------------- B: stb_ds */

typedef struct map_stb {
  void *table; /**< stb_ds hash map of {key, id}; NULL while empty */
} map_stb;

/** @param seed passed to stbds_rand_seed(), which is process global. */
arnm_result map_stb_init(map_stb *map, uint64_t seed);
arnm_result map_stb_get_or_insert(map_stb *map, const uint8_t *key, uint32_t *id, bool *inserted);
bool map_stb_find(const map_stb *map, const uint8_t *key, uint32_t *id);
void map_stb_free(map_stb *map);
/** stb allocates through proto_counted_*; the live bytes come from its counters. */
uint32_t map_stb_count(const map_stb *map);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_BENCH_PROTO_MAPS_H
