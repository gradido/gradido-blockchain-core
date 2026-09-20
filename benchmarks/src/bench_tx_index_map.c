#include "arnm/arena.h"
#include "arnm/mono_timer.h"
#include "bench_chain_data.h"
#include "bench_report.h"
#include "proto/maps.h"
#include "proto/proto_common.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Which map turns a 32 byte public key into the dense id the transaction index stores its per
 * address data under.
 *
 *   B   stb_ds                               malloc through a global macro
 *   C1  linear probing, key in the slot      arnm arena
 *   C2  linear probing, narrow hash in slot  arnm arena
 *   D   sorted array, binary search          arnm arena
 *
 * C1 and C2 run with three hash modes (see proto_hash_mode) and, for the mode that survives
 * every key set, with three load factors.
 *
 * Workloads:
 *   chain      every address reference of a real chain file in transaction order -- what
 *              building the index does: mostly hits, a new key now and then
 *   uniform N  N random keys inserted, then looked up again (hit) and N others (miss)
 *   flood      20k keys crafted against a weak hash: first 8 bytes identical (a transfer
 *              recipient is any 32 bytes, nobody has to own the key), or only the low 24 bits
 *              identical (what grinding key generation reaches cheaply)
 *
 * Each row is the best of several repetitions; "live" is the map as it stands afterwards,
 * "arena" everything the arena handed out -- for C1 and C2 that includes every table a grow
 * superseded, which an arena does not take back. For stb, "arena" is the malloc peak.
 *
 * Usage: bench_tx_index_map [chain file]   (or GRD_BENCH_CHAIN_DATA)
 */

#define ARENA_CAPACITY (1536u * 1024u * 1024u)
#define TARGET_OPS_PER_ROW 2000000u
#define FLOOD_KEY_COUNT 20000u
#define SORTED_MAX_KEYS 100000u
#define MAP_SEED 0x5eed5eed12345678ull

typedef enum variant_kind { KIND_STB, KIND_LPI, KIND_LPN, KIND_SORTED } variant_kind;

typedef struct variant {
  const char *label;
  variant_kind kind;
  proto_hash_mode hash_mode;
  uint8_t load_percent;
  bool in_flood; /**< the flood sets are quadratic for a weak hash; run the main rows only */
} variant;

static const variant variants[] = {
    {"B  stb_ds", KIND_STB, PROTO_HASH_MIX32, 0, true},
    {"C1 inline  raw8  lf75", KIND_LPI, PROTO_HASH_RAW8, 75, true},
    {"C1 inline  mix8  lf75", KIND_LPI, PROTO_HASH_MIX8, 75, true},
    {"C1 inline  mix32 lf75", KIND_LPI, PROTO_HASH_MIX32, 75, true},
    {"C1 inline  mix32 lf50", KIND_LPI, PROTO_HASH_MIX32, 50, false},
    {"C1 inline  mix32 lf88", KIND_LPI, PROTO_HASH_MIX32, 88, false},
    {"C2 narrow  raw8  lf75", KIND_LPN, PROTO_HASH_RAW8, 75, true},
    {"C2 narrow  mix8  lf75", KIND_LPN, PROTO_HASH_MIX8, 75, true},
    {"C2 narrow  mix32 lf75", KIND_LPN, PROTO_HASH_MIX32, 75, true},
    {"C1 inline  sip13 lf75", KIND_LPI, PROTO_HASH_SIP13, 75, true},
    {"C2 narrow  sip13 lf75", KIND_LPN, PROTO_HASH_SIP13, 75, true},
    {"C2 narrow  mix32 lf50", KIND_LPN, PROTO_HASH_MIX32, 50, false},
    {"C2 narrow  mix32 lf88", KIND_LPN, PROTO_HASH_MIX32, 88, false},
    {"D  sorted", KIND_SORTED, PROTO_HASH_RAW8, 0, true},
};
#define VARIANT_COUNT (sizeof(variants) / sizeof(variants[0]))

typedef struct any_map {
  variant_kind kind;
  map_stb stb;
  map_lpi lpi;
  map_lpn lpn;
  map_sorted sorted;
} any_map;

static arnm g_arena;

static void map_open(any_map *map, const variant *v) {
  map->kind = v->kind;
  arnm_result result = ARNM_SUCCESS;
  switch (v->kind) {
  case KIND_STB:
    result = map_stb_init(&map->stb, MAP_SEED);
    break;
  case KIND_LPI:
    result = map_lpi_init(&map->lpi, 0, v->load_percent, v->hash_mode, MAP_SEED, &g_arena);
    break;
  case KIND_LPN:
    result = map_lpn_init(&map->lpn, 0, v->load_percent, v->hash_mode, MAP_SEED, &g_arena);
    break;
  case KIND_SORTED:
    result = map_sorted_init(&map->sorted, 0, &g_arena);
    break;
  }
  if (ARNM_SUCCESS != result) {
    fprintf(stderr, "map init failed: %d\n", (int)result);
    exit(1);
  }
}

static void map_close(any_map *map) {
  switch (map->kind) {
  case KIND_STB:
    map_stb_free(&map->stb);
    break;
  case KIND_LPI:
    map_lpi_free(&map->lpi);
    break;
  case KIND_LPN:
    map_lpn_free(&map->lpn);
    break;
  case KIND_SORTED:
    map_sorted_free(&map->sorted);
    break;
  }
  arnm_reset(&g_arena);
}

static uint64_t map_live_bytes(const any_map *map) {
  switch (map->kind) {
  case KIND_STB:
    return proto_counted_get_stats().current_bytes;
  case KIND_LPI:
    return map_lpi_bytes(&map->lpi);
  case KIND_LPN:
    return map_lpn_bytes(&map->lpn);
  default:
    return map_sorted_bytes(&map->sorted);
  }
}

static uint64_t map_arena_bytes(const any_map *map) {
  if (KIND_STB == map->kind) { return proto_counted_get_stats().peak_bytes; }
  return (uint64_t)(ARENA_CAPACITY - arnm_arena_remaining(&g_arena));
}

static void fail(const char *what, arnm_result result) {
  fprintf(stderr, "%s failed: %d\n", what, (int)result);
  exit(1);
}

/* The loops are spelled out per kind so the hot path holds no dispatch. */

/** get_or_insert every key in @p keys; returns the sum of ids as a cross-variant checksum. */
static uint64_t run_insert(any_map *map, const uint8_t *const *keys, uint32_t count) {
  uint64_t sum = 0;
  uint32_t id = 0;
  arnm_result result = ARNM_SUCCESS;
  switch (map->kind) {
  case KIND_STB:
    for (uint32_t i = 0; i < count && ARNM_SUCCESS == result; ++i) {
      result = map_stb_get_or_insert(&map->stb, keys[i], &id, NULL);
      sum += id;
    }
    break;
  case KIND_LPI:
    for (uint32_t i = 0; i < count && ARNM_SUCCESS == result; ++i) {
      result = map_lpi_get_or_insert(&map->lpi, keys[i], &id, NULL);
      sum += id;
    }
    break;
  case KIND_LPN:
    for (uint32_t i = 0; i < count && ARNM_SUCCESS == result; ++i) {
      result = map_lpn_get_or_insert(&map->lpn, keys[i], &id, NULL);
      sum += id;
    }
    break;
  case KIND_SORTED:
    for (uint32_t i = 0; i < count && ARNM_SUCCESS == result; ++i) {
      result = map_sorted_get_or_insert(&map->sorted, keys[i], &id, NULL);
      sum += id;
    }
    break;
  }
  if (ARNM_SUCCESS != result) { fail("get_or_insert", result); }
  return sum;
}

/** find every key; returns how many were found. */
static uint32_t run_find(const any_map *map, const uint8_t *const *keys, uint32_t count) {
  uint32_t found = 0;
  uint32_t id = 0;
  switch (map->kind) {
  case KIND_STB:
    for (uint32_t i = 0; i < count; ++i) { found += map_stb_find(&map->stb, keys[i], &id); }
    break;
  case KIND_LPI:
    for (uint32_t i = 0; i < count; ++i) { found += map_lpi_find(&map->lpi, keys[i], &id); }
    break;
  case KIND_LPN:
    for (uint32_t i = 0; i < count; ++i) { found += map_lpn_find(&map->lpn, keys[i], &id); }
    break;
  case KIND_SORTED:
    for (uint32_t i = 0; i < count; ++i) { found += map_sorted_find(&map->sorted, keys[i], &id); }
    break;
  }
  return found;
}

static void format_bytes(char *buffer, size_t size, uint64_t bytes) {
  if (bytes < 1024u) {
    snprintf(buffer, size, "%llu B", (unsigned long long)bytes);
  } else if (bytes < 1024u * 1024u) {
    snprintf(buffer, size, "%.1f KiB", (double)bytes / 1024.0);
  } else {
    snprintf(buffer, size, "%.1f MiB", (double)bytes / (1024.0 * 1024.0));
  }
}

static void print_row(
    const char *label, uint64_t nanos, uint32_t ops, uint64_t live, uint64_t arena, const char *note
) {
  char total[BENCH_STRING_BUFFER_SIZE];
  char per_op[BENCH_STRING_BUFFER_SIZE];
  char live_text[BENCH_STRING_BUFFER_SIZE];
  char arena_text[BENCH_STRING_BUFFER_SIZE];
  snprintf(total, sizeof(total), "%.2f ms", (double)nanos / 1e6);
  bench_per_step_string(per_op, sizeof(per_op), ops ? (double)nanos / (double)ops : 0.0);
  if (live || arena) {
    format_bytes(live_text, sizeof(live_text), live);
    format_bytes(arena_text, sizeof(arena_text), arena);
  } else {
    // a find allocates nothing; the insert section above holds the figures
    snprintf(live_text, sizeof(live_text), "-");
    snprintf(arena_text, sizeof(arena_text), "-");
  }
  printf("  %-24s %11s %11s/op %12s %12s  %s\n", label, total, per_op, live_text, arena_text, note);
}

static void print_header(const char *title) {
  printf(
      "\n%s\n  %-24s %11s %14s %12s %12s\n", title, "variant", "total", "per op", "live",
      "arena/peak"
  );
}

static uint32_t repetitions_for(uint32_t ops) {
  uint32_t reps = ops ? TARGET_OPS_PER_ROW / ops : 1u;
  if (reps < 1u) { reps = 1u; }
  if (reps > 50u) { reps = 50u; }
  return reps;
}

/**
 * One key set against every variant: insert @p insert_keys (fresh map per repetition), then
 * find @p hit_keys and @p miss_keys on the last map.
 */
static void run_workload(
    const char *title,
    const uint8_t *const *insert_keys,
    uint32_t insert_count,
    const uint8_t *const *hit_keys,
    uint32_t hit_count,
    const uint8_t *const *miss_keys,
    uint32_t miss_count,
    uint32_t unique_count,
    bool flood
) {
  char heading[160];
  snprintf(
      heading, sizeof(heading), "%s: get_or_insert x%u (%u unique keys)", title, insert_count,
      unique_count
  );
  print_header(heading);

  uint64_t baseline_checksum = 0;
  bool have_baseline = false;
  // results for the hit and miss sections, printed after all insert rows
  uint64_t hit_nanos[VARIANT_COUNT] = {0};
  uint64_t miss_nanos[VARIANT_COUNT] = {0};
  bool ran[VARIANT_COUNT] = {false};
  uint32_t found_hits[VARIANT_COUNT] = {0};
  uint32_t found_misses[VARIANT_COUNT] = {0};

  for (size_t v = 0; v < VARIANT_COUNT; ++v) {
    const variant *var = &variants[v];
    if (flood && !var->in_flood) { continue; }
    if (KIND_SORTED == var->kind && unique_count > SORTED_MAX_KEYS) {
      printf("  %-24s skipped: O(n) insert past %u keys\n", var->label, SORTED_MAX_KEYS);
      continue;
    }
    uint32_t reps = flood ? 1u : repetitions_for(insert_count);
    uint64_t best = UINT64_MAX;
    uint64_t checksum = 0;
    any_map map;
    for (uint32_t r = 0; r < reps; ++r) {
      proto_counted_reset_peak();
      map_open(&map, var);
      arnm_mono_timer timer;
      arnm_mono_timer_reset(&timer);
      checksum = run_insert(&map, insert_keys, insert_count);
      uint64_t nanos = (uint64_t)arnm_mono_timer_nanos(timer);
      if (nanos < best) { best = nanos; }
      if (r + 1u < reps) { map_close(&map); }
    }
    const char *note = "";
    if (!have_baseline) {
      baseline_checksum = checksum;
      have_baseline = true;
    } else if (checksum != baseline_checksum) {
      note = "IDS DIFFER";
    }
    print_row(var->label, best, insert_count, map_live_bytes(&map), map_arena_bytes(&map), note);

    uint32_t find_reps = flood ? 1u : repetitions_for(hit_count + miss_count);
    hit_nanos[v] = UINT64_MAX;
    miss_nanos[v] = UINT64_MAX;
    for (uint32_t r = 0; r < find_reps; ++r) {
      arnm_mono_timer timer;
      arnm_mono_timer_reset(&timer);
      found_hits[v] = run_find(&map, hit_keys, hit_count);
      uint64_t nanos = (uint64_t)arnm_mono_timer_nanos(timer);
      if (nanos < hit_nanos[v]) { hit_nanos[v] = nanos; }
      arnm_mono_timer_reset(&timer);
      found_misses[v] = run_find(&map, miss_keys, miss_count);
      nanos = (uint64_t)arnm_mono_timer_nanos(timer);
      if (nanos < miss_nanos[v]) { miss_nanos[v] = nanos; }
    }
    ran[v] = true;
    map_close(&map);
  }

  snprintf(heading, sizeof(heading), "%s: find hit x%u", title, hit_count);
  print_header(heading);
  for (size_t v = 0; v < VARIANT_COUNT; ++v) {
    if (!ran[v]) { continue; }
    print_row(
        variants[v].label, hit_nanos[v], hit_count, 0, 0,
        found_hits[v] == hit_count ? "" : "NOT ALL FOUND"
    );
  }
  snprintf(heading, sizeof(heading), "%s: find miss x%u", title, miss_count);
  print_header(heading);
  for (size_t v = 0; v < VARIANT_COUNT; ++v) {
    if (!ran[v]) { continue; }
    print_row(
        variants[v].label, miss_nanos[v], miss_count, 0, 0, found_misses[v] == 0 ? "" : "FALSE HITS"
    );
  }
}

typedef struct key_set {
  uint8_t *bytes;
  const uint8_t **refs;
  uint32_t count;
} key_set;

static void key_set_alloc(key_set *set, uint32_t count) {
  set->bytes = (uint8_t *)malloc((size_t)count * PROTO_KEY_SIZE);
  set->refs = (const uint8_t **)malloc((size_t)count * sizeof(uint8_t *));
  if (!set->bytes || !set->refs) {
    fprintf(stderr, "out of memory\n");
    exit(1);
  }
  set->count = count;
  for (uint32_t i = 0; i < count; ++i) { set->refs[i] = set->bytes + (size_t)i * PROTO_KEY_SIZE; }
}

static void key_set_free(key_set *set) {
  free(set->bytes);
  free(set->refs);
  memset(set, 0, sizeof(*set));
}

static void shuffle_refs(const uint8_t **refs, uint32_t count, uint64_t *state) {
  for (uint32_t i = count; i > 1u; --i) {
    uint32_t j = (uint32_t)(bench_random_next(state) % i);
    const uint8_t *swap = refs[i - 1u];
    refs[i - 1u] = refs[j];
    refs[j] = swap;
  }
}

static void run_uniform(uint32_t count, uint64_t *state) {
  key_set keys, hits, misses;
  key_set_alloc(&keys, count);
  key_set_alloc(&hits, count);
  key_set_alloc(&misses, count);
  for (uint32_t i = 0; i < count; ++i) {
    bench_random_key(state, keys.bytes + (size_t)i * PROTO_KEY_SIZE);
    bench_random_key(state, misses.bytes + (size_t)i * PROTO_KEY_SIZE);
  }
  memcpy(hits.refs, keys.refs, (size_t)count * sizeof(uint8_t *));
  shuffle_refs(hits.refs, count, state);
  char title[64];
  snprintf(title, sizeof(title), "uniform %uk", count / 1000u);
  run_workload(title, keys.refs, count, hits.refs, count, misses.refs, count, count, false);
  key_set_free(&keys);
  key_set_free(&hits);
  key_set_free(&misses);
}

/** @p shared_bits of the first word identical across all keys, the rest random. */
static void run_flood(const char *title, unsigned shared_bits, uint64_t *state) {
  key_set keys, hits, misses;
  key_set_alloc(&keys, FLOOD_KEY_COUNT);
  key_set_alloc(&hits, FLOOD_KEY_COUNT);
  key_set_alloc(&misses, FLOOD_KEY_COUNT);
  uint64_t shared = 0x0123456789abcdefull;
  uint64_t shared_mask = shared_bits >= 64u ? ~0ull : ((1ull << shared_bits) - 1u);
  for (uint32_t i = 0; i < FLOOD_KEY_COUNT; ++i) {
    for (int s = 0; s < 2; ++s) {
      uint8_t *key = (s ? misses.bytes : keys.bytes) + (size_t)i * PROTO_KEY_SIZE;
      bench_random_key(state, key);
      uint64_t first = proto_load64(key);
      first = (first & ~shared_mask) | (shared & shared_mask);
      memcpy(key, &first, sizeof(first));
    }
  }
  memcpy(hits.refs, keys.refs, (size_t)FLOOD_KEY_COUNT * sizeof(uint8_t *));
  shuffle_refs(hits.refs, FLOOD_KEY_COUNT, state);
  run_workload(
      title, keys.refs, FLOOD_KEY_COUNT, hits.refs, FLOOD_KEY_COUNT, misses.refs, FLOOD_KEY_COUNT,
      FLOOD_KEY_COUNT, true
  );
  key_set_free(&keys);
  key_set_free(&hits);
  key_set_free(&misses);
}

static void run_chain(const bench_chain_data *data, uint64_t *state) {
  uint32_t references = 0;
  for (uint32_t i = 0; i < data->count; ++i) { references += data->records[i].address_count; }
  const uint8_t **refs = (const uint8_t **)malloc((size_t)references * sizeof(uint8_t *));
  if (!refs) {
    fprintf(stderr, "out of memory\n");
    exit(1);
  }
  uint32_t r = 0;
  for (uint32_t i = 0; i < data->count; ++i) {
    for (uint8_t a = 0; a < data->records[i].address_count; ++a) {
      refs[r++] = data->records[i].addresses[a].key;
    }
  }

  // unique keys, first sighting order, from a throwaway map
  map_lpn unique_map;
  map_lpn_init(&unique_map, 0, 75, PROTO_HASH_MIX32, MAP_SEED, NULL);
  key_set hits, misses;
  key_set_alloc(&hits, references);
  uint32_t unique = 0;
  for (uint32_t i = 0; i < references; ++i) {
    bool inserted = false;
    uint32_t id = 0;
    map_lpn_get_or_insert(&unique_map, refs[i], &id, &inserted);
    if (inserted) { hits.refs[unique++] = refs[i]; }
  }
  map_lpn_free(&unique_map);
  shuffle_refs(hits.refs, unique, state);
  key_set_alloc(&misses, unique);
  for (uint32_t i = 0; i < unique; ++i) {
    bench_random_key(state, misses.bytes + (size_t)i * PROTO_KEY_SIZE);
  }
  run_workload("chain", refs, references, hits.refs, unique, misses.refs, unique, unique, false);
  free(refs);
  key_set_free(&hits);
  key_set_free(&misses);
}

int main(int argc, char **argv) {
  arnm_mono_timer total;
  arnm_mono_timer_reset(&total);
  if (ARNM_SUCCESS != arnm_init_arena(&g_arena, ARENA_CAPACITY)) {
    fprintf(stderr, "cannot reserve the benchmark arena\n");
    return 1;
  }
  uint64_t state = 0x7a11ce5eedull;

  printf(
      "public key -> dense id; best of repeated runs; live = map afterwards, arena = all "
      "handed out (stb: malloc peak)\n"
  );

  const char *path = bench_chain_data_path(argc, argv);
  bench_chain_data data;
  if (path && ARNM_SUCCESS == bench_chain_data_load(&data, path, bench_default_community_uuid)) {
    printf(
        "chain file %s: %u transactions, %u decode failures\n", path, data.count,
        data.decode_failures
    );
    run_chain(&data, &state);
    bench_chain_data_free(&data);
  } else {
    printf("no chain file (argument or GRD_BENCH_CHAIN_DATA), chain workload skipped\n");
  }

  run_uniform(10000u, &state);
  run_uniform(100000u, &state);
  run_uniform(1000000u, &state);
  run_flood("flood first 8 bytes equal", 64u, &state);
  run_flood("flood low 24 bits equal", 24u, &state);

  arnm_release(&g_arena);
  char buffer[BENCH_STRING_BUFFER_SIZE];
  arnm_mono_timer_string(buffer, BENCH_STRING_BUFFER_SIZE, total);
  printf("\nall benchmarks: %s\n", buffer);
  return 0;
}
