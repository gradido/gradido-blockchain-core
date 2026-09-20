#include "maps.h"

#include <string.h>

/*
 * D: no hash at all. Entries stay sorted by key; a lookup is a binary search over memcmp, an
 * insert a binary search and a memmove of everything behind the insertion point. Lookups are
 * O(log n), inserts O(n) -- the benchmark is there to say at which key count that stops being
 * acceptable, not to argue that it is.
 */

static arnm_result sorted_reserve(map_sorted *map, uint32_t capacity) {
  if (capacity <= map->capacity) { return ARNM_SUCCESS; }
  uint64_t new_bytes = (uint64_t)capacity * sizeof(map_sorted_entry);
  if (new_bytes > UINT32_MAX) { return ARNM_ERROR_ARITHMETIC_OVERFLOW; }
  uint8_t *memory = (uint8_t *)map->entries;
  arnm_result result = arnm_realloc(
      &memory, (uint32_t)((uint64_t)map->capacity * sizeof(map_sorted_entry)), (uint32_t)new_bytes,
      map->allocator
  );
  if (ARNM_SUCCESS != result && ARNM_WARNING_ARENA_MEMORY_NOT_RECLAIMED != result) {
    return result;
  }
  map->entries = (map_sorted_entry *)(void *)memory;
  map->capacity = capacity;
  return ARNM_SUCCESS;
}

arnm_result map_sorted_init(map_sorted *map, uint32_t initial_capacity, arnm *allocator) {
  if (!map) { return ARNM_ERROR_NULL_POINTER; }
  map->entries = NULL;
  map->capacity = 0;
  map->count = 0;
  map->allocator = allocator;
  return initial_capacity ? sorted_reserve(map, initial_capacity) : ARNM_SUCCESS;
}

/** First index whose key is not less than @p key; sets @p found when it is equal. */
static uint32_t sorted_lower_bound(const map_sorted *map, const uint8_t *key, bool *found) {
  uint32_t low = 0;
  uint32_t high = map->count;
  while (low < high) {
    uint32_t middle = low + (high - low) / 2u;
    int order = memcmp(map->entries[middle].key, key, PROTO_KEY_SIZE);
    if (order < 0) {
      low = middle + 1u;
    } else if (order > 0) {
      high = middle;
    } else {
      *found = true;
      return middle;
    }
  }
  *found = false;
  return low;
}

arnm_result map_sorted_get_or_insert(
    map_sorted *map, const uint8_t *key, uint32_t *id, bool *inserted
) {
  if (!map || !key || !id) { return ARNM_ERROR_NULL_POINTER; }
  bool found = false;
  uint32_t position = sorted_lower_bound(map, key, &found);
  if (found) {
    *id = map->entries[position].id;
    if (inserted) { *inserted = false; }
    return ARNM_SUCCESS;
  }
  if (map->count == map->capacity) {
    if (map->capacity >= (1u << 30)) { return ARNM_ERROR_RESOURCE_EXHAUSTED; }
    arnm_result result = sorted_reserve(map, map->capacity ? map->capacity * 2u : 16u);
    if (ARNM_SUCCESS != result) { return result; }
  }
  memmove(
      &map->entries[position + 1u], &map->entries[position],
      (size_t)(map->count - position) * sizeof(map_sorted_entry)
  );
  memcpy(map->entries[position].key, key, PROTO_KEY_SIZE);
  map->entries[position].id = map->count;
  *id = map->count++;
  if (inserted) { *inserted = true; }
  return ARNM_SUCCESS;
}

bool map_sorted_find(const map_sorted *map, const uint8_t *key, uint32_t *id) {
  if (!map || !key) { return false; }
  bool found = false;
  uint32_t position = sorted_lower_bound(map, key, &found);
  if (found && id) { *id = map->entries[position].id; }
  return found;
}

void map_sorted_free(map_sorted *map) {
  if (!map) { return; }
  if (map->entries) {
    (void)arnm_free(
        (uint8_t *)map->entries, (uint32_t)((uint64_t)map->capacity * sizeof(map_sorted_entry)),
        map->allocator
    );
  }
  map->entries = NULL;
  map->capacity = 0;
  map->count = 0;
}

uint64_t map_sorted_bytes(const map_sorted *map) {
  return map ? (uint64_t)map->capacity * sizeof(map_sorted_entry) : 0u;
}
