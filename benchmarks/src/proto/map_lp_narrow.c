#include "maps.h"

#include <string.h>

/*
 * C2: linear probing over 8 byte slots. The slot keeps a 32 bit folded hash and the id; the
 * key itself sits in an arnm_bvec at its id. A probe compares four bytes per occupied slot and
 * reads the key only when those match -- the "narrow hash" layout.
 *
 * The same 32 bits pick the home slot (low bits under the mask) and filter the comparison (the
 * bits above it). That is what lets a grow rehash from the slots alone, without touching a
 * single key, and it caps the table at 2^31 slots, far past anything an index holds.
 */

#define LPN_KEYS_BUCKET_LOG2 12

static uint32_t lpn_fold(uint64_t hash) {
  return (uint32_t)(hash ^ (hash >> 32));
}

static uint32_t lpn_grow_at(uint32_t capacity, uint8_t load_percent) {
  uint64_t grow_at = (uint64_t)capacity * load_percent / 100u;
  if (grow_at >= capacity) { grow_at = capacity - 1u; }
  return (uint32_t)grow_at;
}

static arnm_result lpn_allocate(map_lpn_slot **slots, uint32_t capacity, arnm *allocator) {
  uint64_t bytes = (uint64_t)capacity * sizeof(map_lpn_slot);
  if (bytes > UINT32_MAX) { return ARNM_ERROR_ARITHMETIC_OVERFLOW; }
  uint8_t *memory = NULL;
  arnm_result result = arnm_alloc(&memory, (uint32_t)bytes, allocator);
  if (ARNM_SUCCESS != result) { return result; }
  memset(memory, 0xff, (size_t)bytes);
  *slots = (map_lpn_slot *)(void *)memory;
  return ARNM_SUCCESS;
}

static void lpn_release_table(map_lpn_slot *slots, uint32_t capacity, arnm *allocator) {
  if (!slots) { return; }
  (void)arnm_free(
      (uint8_t *)slots, (uint32_t)((uint64_t)capacity * sizeof(map_lpn_slot)), allocator
  );
}

arnm_result map_lpn_init(
    map_lpn *map,
    uint32_t initial_capacity,
    uint8_t load_percent,
    proto_hash_mode hash_mode,
    uint64_t seed,
    arnm *allocator
) {
  if (!map) { return ARNM_ERROR_NULL_POINTER; }
  if (load_percent < 10 || load_percent > 95) { return ARNM_ERROR_INVALID_PARAM; }
  arnm_result result =
      arnm_bvec_init(&map->keys, LPN_KEYS_BUCKET_LOG2, 0, PROTO_KEY_SIZE, allocator);
  if (ARNM_SUCCESS != result) { return result; }
  map->slots = NULL;
  map->capacity = 0;
  map->count = 0;
  map->grow_at = 0;
  map->load_percent = load_percent;
  map->hash_mode = hash_mode;
  map->seed = seed;
  map->allocator = allocator;
  if (initial_capacity) {
    uint32_t capacity = proto_next_pow2_u32(initial_capacity);
    if (capacity < 8u) { capacity = 8u; }
    result = lpn_allocate(&map->slots, capacity, allocator);
    if (ARNM_SUCCESS != result) { return result; }
    map->capacity = capacity;
    map->grow_at = lpn_grow_at(capacity, load_percent);
  }
  return ARNM_SUCCESS;
}

static arnm_result lpn_grow(map_lpn *map) {
  uint32_t new_capacity = map->capacity ? map->capacity * 2u : 8u;
  if (!new_capacity || new_capacity > (1u << 31)) { return ARNM_ERROR_ARITHMETIC_OVERFLOW; }
  map_lpn_slot *new_slots = NULL;
  arnm_result result = lpn_allocate(&new_slots, new_capacity, map->allocator);
  if (ARNM_SUCCESS != result) { return result; }

  uint32_t mask = new_capacity - 1u;
  for (uint32_t i = 0; i < map->capacity; ++i) {
    const map_lpn_slot slot = map->slots[i];
    if (PROTO_MAP_EMPTY_ID == slot.id) { continue; }
    uint32_t index = slot.hash32 & mask;
    while (PROTO_MAP_EMPTY_ID != new_slots[index].id) { index = (index + 1u) & mask; }
    new_slots[index] = slot;
  }
  lpn_release_table(map->slots, map->capacity, map->allocator);
  map->slots = new_slots;
  map->capacity = new_capacity;
  map->grow_at = lpn_grow_at(new_capacity, map->load_percent);
  return ARNM_SUCCESS;
}

arnm_result map_lpn_get_or_insert(map_lpn *map, const uint8_t *key, uint32_t *id, bool *inserted) {
  if (!map || !key || !id) { return ARNM_ERROR_NULL_POINTER; }
  if (map->count >= map->grow_at) {
    if (PROTO_MAP_EMPTY_ID - 1u == map->count) { return ARNM_ERROR_RESOURCE_EXHAUSTED; }
    arnm_result result = lpn_grow(map);
    if (ARNM_SUCCESS != result) { return result; }
  }
  uint32_t hash32 = lpn_fold(proto_hash_key(key, map->seed, map->hash_mode));
  uint32_t mask = map->capacity - 1u;
  uint32_t index = hash32 & mask;
  for (;;) {
    map_lpn_slot *slot = &map->slots[index];
    if (PROTO_MAP_EMPTY_ID == slot->id) {
      // the key goes in first: a failed push must not leave a slot pointing at nothing
      void *key_slot = NULL;
      arnm_result result = arnm_bvec_emplace(&map->keys, &key_slot);
      if (ARNM_SUCCESS != result) { return result; }
      memcpy(key_slot, key, PROTO_KEY_SIZE);
      slot->hash32 = hash32;
      slot->id = map->count++;
      *id = slot->id;
      if (inserted) { *inserted = true; }
      return ARNM_SUCCESS;
    }
    if (slot->hash32 == hash32 &&
        0 == memcmp(arnm_bvec_get(&map->keys, slot->id), key, PROTO_KEY_SIZE)) {
      *id = slot->id;
      if (inserted) { *inserted = false; }
      return ARNM_SUCCESS;
    }
    index = (index + 1u) & mask;
  }
}

bool map_lpn_find(const map_lpn *map, const uint8_t *key, uint32_t *id) {
  if (!map || !key || !map->capacity) { return false; }
  uint32_t hash32 = lpn_fold(proto_hash_key(key, map->seed, map->hash_mode));
  uint32_t mask = map->capacity - 1u;
  uint32_t index = hash32 & mask;
  for (;;) {
    const map_lpn_slot *slot = &map->slots[index];
    if (PROTO_MAP_EMPTY_ID == slot->id) { return false; }
    if (slot->hash32 == hash32 &&
        0 == memcmp(arnm_bvec_get(&map->keys, slot->id), key, PROTO_KEY_SIZE)) {
      if (id) { *id = slot->id; }
      return true;
    }
    index = (index + 1u) & mask;
  }
}

void map_lpn_free(map_lpn *map) {
  if (!map) { return; }
  lpn_release_table(map->slots, map->capacity, map->allocator);
  arnm_bvec_free(&map->keys);
  map->slots = NULL;
  map->capacity = 0;
  map->count = 0;
  map->grow_at = 0;
}

uint64_t map_lpn_bytes(const map_lpn *map) {
  if (!map) { return 0u; }
  uint64_t key_bytes = (uint64_t)arnm_bvec_bucket_count(&map->keys) *
                       ((uint64_t)PROTO_KEY_SIZE << LPN_KEYS_BUCKET_LOG2);
  return (uint64_t)map->capacity * sizeof(map_lpn_slot) + key_bytes;
}
