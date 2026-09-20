#include "maps.h"

#include <string.h>

/*
 * C1: linear probing with the whole key in the slot. A probe compares 32 bytes at every
 * occupied slot it passes, but never leaves the table -- the comparison partner of C2, which
 * compares four bytes first and pays a second memory access for the key only on a match.
 */

static uint32_t lpi_grow_at(uint32_t capacity, uint8_t load_percent) {
  uint64_t grow_at = (uint64_t)capacity * load_percent / 100u;
  if (grow_at >= capacity) { grow_at = capacity - 1u; }
  return (uint32_t)grow_at;
}

static arnm_result lpi_allocate(map_lpi_slot **slots, uint32_t capacity, arnm *allocator) {
  uint64_t bytes = (uint64_t)capacity * sizeof(map_lpi_slot);
  if (bytes > UINT32_MAX) { return ARNM_ERROR_ARITHMETIC_OVERFLOW; }
  uint8_t *memory = NULL;
  arnm_result result = arnm_alloc(&memory, (uint32_t)bytes, allocator);
  if (ARNM_SUCCESS != result) { return result; }
  // 0xff everywhere puts PROTO_MAP_EMPTY_ID into every id; the key bytes are never read there
  memset(memory, 0xff, (size_t)bytes);
  *slots = (map_lpi_slot *)(void *)memory;
  return ARNM_SUCCESS;
}

static void lpi_release_table(map_lpi_slot *slots, uint32_t capacity, arnm *allocator) {
  if (!slots) { return; }
  // behind an arena an older table does not come back; that is measured, not an error
  (void)arnm_free(
      (uint8_t *)slots, (uint32_t)((uint64_t)capacity * sizeof(map_lpi_slot)), allocator
  );
}

arnm_result map_lpi_init(
    map_lpi *map,
    uint32_t initial_capacity,
    uint8_t load_percent,
    proto_hash_mode hash_mode,
    uint64_t seed,
    arnm *allocator
) {
  if (!map) { return ARNM_ERROR_NULL_POINTER; }
  if (load_percent < 10 || load_percent > 95) { return ARNM_ERROR_INVALID_PARAM; }
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
    arnm_result result = lpi_allocate(&map->slots, capacity, allocator);
    if (ARNM_SUCCESS != result) { return result; }
    map->capacity = capacity;
    map->grow_at = lpi_grow_at(capacity, load_percent);
  }
  return ARNM_SUCCESS;
}

static arnm_result lpi_grow(map_lpi *map) {
  uint32_t new_capacity = map->capacity ? map->capacity * 2u : 8u;
  if (!new_capacity) { return ARNM_ERROR_ARITHMETIC_OVERFLOW; }
  map_lpi_slot *new_slots = NULL;
  arnm_result result = lpi_allocate(&new_slots, new_capacity, map->allocator);
  if (ARNM_SUCCESS != result) { return result; }

  uint32_t mask = new_capacity - 1u;
  for (uint32_t i = 0; i < map->capacity; ++i) {
    const map_lpi_slot *slot = &map->slots[i];
    if (PROTO_MAP_EMPTY_ID == slot->id) { continue; }
    // every key is distinct already: find a hole, compare nothing
    uint32_t index = (uint32_t)proto_hash_key(slot->key, map->seed, map->hash_mode) & mask;
    while (PROTO_MAP_EMPTY_ID != new_slots[index].id) { index = (index + 1u) & mask; }
    new_slots[index] = *slot;
  }
  lpi_release_table(map->slots, map->capacity, map->allocator);
  map->slots = new_slots;
  map->capacity = new_capacity;
  map->grow_at = lpi_grow_at(new_capacity, map->load_percent);
  return ARNM_SUCCESS;
}

arnm_result map_lpi_get_or_insert(map_lpi *map, const uint8_t *key, uint32_t *id, bool *inserted) {
  if (!map || !key || !id) { return ARNM_ERROR_NULL_POINTER; }
  if (map->count >= map->grow_at) {
    if (PROTO_MAP_EMPTY_ID - 1u == map->count) { return ARNM_ERROR_RESOURCE_EXHAUSTED; }
    // grow before probing, so the slot found below is still the right one afterwards
    arnm_result result = lpi_grow(map);
    if (ARNM_SUCCESS != result) { return result; }
  }
  uint32_t mask = map->capacity - 1u;
  uint32_t index = (uint32_t)proto_hash_key(key, map->seed, map->hash_mode) & mask;
  for (;;) {
    map_lpi_slot *slot = &map->slots[index];
    if (PROTO_MAP_EMPTY_ID == slot->id) {
      memcpy(slot->key, key, PROTO_KEY_SIZE);
      slot->id = map->count++;
      *id = slot->id;
      if (inserted) { *inserted = true; }
      return ARNM_SUCCESS;
    }
    if (0 == memcmp(slot->key, key, PROTO_KEY_SIZE)) {
      *id = slot->id;
      if (inserted) { *inserted = false; }
      return ARNM_SUCCESS;
    }
    index = (index + 1u) & mask;
  }
}

bool map_lpi_find(const map_lpi *map, const uint8_t *key, uint32_t *id) {
  if (!map || !key || !map->capacity) { return false; }
  uint32_t mask = map->capacity - 1u;
  uint32_t index = (uint32_t)proto_hash_key(key, map->seed, map->hash_mode) & mask;
  for (;;) {
    const map_lpi_slot *slot = &map->slots[index];
    if (PROTO_MAP_EMPTY_ID == slot->id) { return false; }
    if (0 == memcmp(slot->key, key, PROTO_KEY_SIZE)) {
      if (id) { *id = slot->id; }
      return true;
    }
    index = (index + 1u) & mask;
  }
}

void map_lpi_free(map_lpi *map) {
  if (!map) { return; }
  lpi_release_table(map->slots, map->capacity, map->allocator);
  map->slots = NULL;
  map->capacity = 0;
  map->count = 0;
  map->grow_at = 0;
}

uint64_t map_lpi_bytes(const map_lpi *map) {
  return map ? (uint64_t)map->capacity * sizeof(map_lpi_slot) : 0u;
}
