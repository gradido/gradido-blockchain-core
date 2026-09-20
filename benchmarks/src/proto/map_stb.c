/*
 * B: stb_ds. Compiled without the project's warning flags -- the macros expand stb's own code
 * into this translation unit, and -Wconversion there reports stb, not us.
 *
 * stb_ds allocates through STBDS_REALLOC/STBDS_FREE, two macros with no allocator argument that
 * could carry an arnm handle. They are routed to the counting malloc wrapper so the benchmark
 * can report what stb holds; that is as close to an arena as stb gets.
 */
#include "maps.h"

#include <stdlib.h>
#include <string.h>

#define STBDS_REALLOC(context, pointer, size) proto_counted_realloc(pointer, size)
#define STBDS_FREE(context, pointer) proto_counted_free(pointer)
#define STB_DS_IMPLEMENTATION
#include "stb/stb_ds.h"

typedef struct stb_key {
  uint8_t bytes[PROTO_KEY_SIZE];
} stb_key;

typedef struct stb_entry {
  stb_key key;
  uint32_t value;
} stb_entry;

arnm_result map_stb_init(map_stb *map, uint64_t seed) {
  if (!map) { return ARNM_ERROR_NULL_POINTER; }
  map->table = NULL;
  stbds_rand_seed((size_t)seed);
  return ARNM_SUCCESS;
}

arnm_result map_stb_get_or_insert(map_stb *map, const uint8_t *key, uint32_t *id, bool *inserted) {
  if (!map || !key || !id) { return ARNM_ERROR_NULL_POINTER; }
  stb_entry *table = (stb_entry *)map->table;
  stb_key lookup;
  memcpy(lookup.bytes, key, PROTO_KEY_SIZE);
  ptrdiff_t index = hmgeti(table, lookup);
  if (index >= 0) {
    *id = table[index].value;
    if (inserted) { *inserted = false; }
    return ARNM_SUCCESS;
  }
  uint32_t new_id = (uint32_t)hmlen(table);
  hmput(table, lookup, new_id);
  map->table = table;
  *id = new_id;
  if (inserted) { *inserted = true; }
  return ARNM_SUCCESS;
}

bool map_stb_find(const map_stb *map, const uint8_t *key, uint32_t *id) {
  if (!map || !key || !map->table) { return false; }
  stb_entry *table = (stb_entry *)map->table;
  stb_key lookup;
  memcpy(lookup.bytes, key, PROTO_KEY_SIZE);
  ptrdiff_t index = hmgeti(table, lookup);
  if (index < 0) { return false; }
  if (id) { *id = table[index].value; }
  return true;
}

void map_stb_free(map_stb *map) {
  if (!map) { return; }
  stb_entry *table = (stb_entry *)map->table;
  hmfree(table);
  map->table = NULL;
}

uint32_t map_stb_count(const map_stb *map) {
  if (!map || !map->table) { return 0; }
  stb_entry *table = (stb_entry *)map->table;
  return (uint32_t)hmlen(table);
}
