#include "arnm/graded_block_pool.h"
#include "arnm/mono_timer.h"
#include "arnm/roaring_bitmap.h"
#include "arnm/roaring_ops.h"
#include "arnm/roaring_query.h"
#include "bench_chain_data.h"
#include "proto/maps.h"
#include "proto/proto_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * What the sets of an ordinary address look like in arnm/roaring_bitmap.h, and what
 * "the newest 20 transactions an address is involved in" touches and costs -- against the same
 * transactions kept as one plain sorted uint32_t array per address.
 *
 * Same chains and the same sample as bench_tx_index_bitmap: every (addresses / 1000)th address.
 *
 * Usage: bench_tx_index_layout [chain file]   (or GRD_BENCH_CHAIN_DATA)
 */

#define SAMPLE_KEYS 1000u
#define PAGE 20u
#define WARM_REPEAT 50u

typedef struct address_sets {
  arnm_roaring_bitmap sets[3]; /* balance, signer, other */
  uint32_t *involved;          /* the union as a sorted array, the comparison */
  uint32_t involved_count;
} address_sets;

static arnm_graded_block_pool g_pool;
static arnm_graded_block_pool g_array_pool;
static arnm_graded_block_pool g_scratch_pool;
static arnm_roaring_bitmap g_transfer; /* every transfer of the chain, the type set */
static volatile uint64_t g_sink;

static void check(arnm_result result, const char *what) {
  if (ARNM_SUCCESS != result) {
    fprintf(stderr, "%s failed: %d\n", what, (int)result);
    exit(1);
  }
}

static uint64_t set_bytes(const arnm_roaring_bitmap *set) {
  if (!set->containers) { return 0; }
  uint64_t bytes = (uint64_t)1u << set->directory_log2;
  if (!set->count) { return bytes; } /* sparse: the values' block alone */
  for (uint32_t i = 0; i < set->count; ++i) {
    bytes += (uint64_t)1u << set->containers[i].block_log2;
  }
  return bytes;
}

/* the containers "newest 20" reads: keys from the top until 20 values of the union are had */
static void newest_footprint(const address_sets *a, uint32_t *keys, uint32_t *containers) {
  *keys = 0;
  *containers = 0;
  uint32_t position[3];
  for (int s = 0; s < 3; ++s) {
    position[s] = a->sets[s].count;
  } /* sparse sets: 0, nothing to walk */
  uint32_t values = 0;
  while (values < PAGE) {
    int best = -1;
    uint16_t key = 0;
    for (int s = 0; s < 3; ++s) {
      if (!position[s]) { continue; }
      const uint16_t k = a->sets[s].containers[position[s] - 1u].key;
      if (best < 0 || k > key) {
        key = k;
        best = s;
      }
    }
    if (best < 0) { break; }
    const arnm_roaring_bitmap *list[3] = {&a->sets[0], &a->sets[1], &a->sets[2]};
    arnm_roaring_query query = {0};
    query.any = list;
    query.any_count = 3;
    query.min = (uint32_t)key << 16;
    query.max = ((uint32_t)key << 16) | 0xffffu;
    uint64_t in_key = 0;
    check(arnm_roaring_query_cardinality(&query, &in_key), "union count");
    for (int s = 0; s < 3; ++s) {
      if (position[s] && a->sets[s].containers[position[s] - 1u].key == key) {
        position[s]--;
        (*containers)++;
      }
    }
    (*keys)++;
    values += (uint32_t)in_key;
  }
}

static uint64_t time_roaring(
    const address_sets *addresses, const uint32_t *sample, uint32_t count, uint32_t repeat
) {
  uint32_t out[PAGE];
  uint64_t sum = 0;
  arnm_mono_timer timer;
  arnm_mono_timer_reset(&timer);
  for (uint32_t r = 0; r < repeat; ++r) {
    for (uint32_t k = 0; k < count; ++k) {
      const address_sets *a = &addresses[sample[k]];
      const arnm_roaring_bitmap *list[3] = {&a->sets[0], &a->sets[1], &a->sets[2]};
      uint32_t written = 0;
      {
        arnm_roaring_query query = {0};
        query.any = list;
        query.any_count = 3;
        query.max = UINT32_MAX;
        check(arnm_roaring_query_page(&query, 0, PAGE, true, out, &written), "page");
      }
      for (uint32_t i = 0; i < written; ++i) { sum = sum * 31u + out[i]; }
    }
  }
  const uint64_t nanos = (uint64_t)arnm_mono_timer_nanos(timer);
  g_sink = sum;
  return nanos;
}

static uint64_t time_array(
    const address_sets *addresses, const uint32_t *sample, uint32_t count, uint32_t repeat
) {
  uint64_t sum = 0;
  arnm_mono_timer timer;
  arnm_mono_timer_reset(&timer);
  for (uint32_t r = 0; r < repeat; ++r) {
    for (uint32_t k = 0; k < count; ++k) {
      const address_sets *a = &addresses[sample[k]];
      const uint32_t take = a->involved_count < PAGE ? a->involved_count : PAGE;
      for (uint32_t i = 0; i < take; ++i) {
        sum = sum * 31u + a->involved[a->involved_count - 1u - i];
      }
    }
  }
  const uint64_t nanos = (uint64_t)arnm_mono_timer_nanos(timer);
  g_sink = sum;
  return nanos;
}

/* the one function the profile looks at: nothing else calls it */
__attribute__((noinline)) static uint64_t time_union_count(
    const address_sets *addresses, const uint32_t *keys, uint32_t count, uint32_t min, uint32_t max
) {
  uint64_t sum = 0;
  arnm_mono_timer timer;
  arnm_mono_timer_reset(&timer);
  for (uint32_t r = 0; r < 50u; ++r) {
    for (uint32_t k = 0; k < count; ++k) {
      const address_sets *a = &addresses[keys[k]];
      const arnm_roaring_bitmap *list[3] = {&a->sets[0], &a->sets[1], &a->sets[2]};
      uint64_t n = 0;
      {
        arnm_roaring_query query = {0};
        query.any = list;
        query.any_count = 3;
        query.min = min;
        query.max = max;
        check(arnm_roaring_query_cardinality(&query, &n), "union count");
      }
      sum += n;
    }
  }
  const uint64_t nanos = (uint64_t)arnm_mono_timer_nanos(timer);
  g_sink = sum;
  return nanos;
}

/* balance AND TRANSFER in [min, max], its count and the second page of 20 -- the row
   "balance+TRANSFER+range, count+page 2" of bench_tx_index_bitmap, the scratch pool reset first */
__attribute__((noinline)) static uint64_t time_balance_transfer(
    const address_sets *addresses, const uint32_t *keys, uint32_t count, uint32_t min, uint32_t max
) {
  uint64_t sum = 0;
  uint32_t page[PAGE];
  arnm_mono_timer timer;
  arnm_mono_timer_reset(&timer);
  for (uint32_t r = 0; r < 50u; ++r) {
    for (uint32_t k = 0; k < count; ++k) {
      if (!getenv("LAYOUT_FREE_RESULT")) { arnm_graded_block_pool_reset(&g_scratch_pool); }
      const arnm_roaring_bitmap *balance = &addresses[keys[k]].sets[0];
      const bool balance_first = balance->cardinality <= g_transfer.cardinality;
      arnm_roaring_bitmap result;
      arnm_roaring_init(&result);
      check(
          arnm_roaring_and(
              &result, balance_first ? balance : &g_transfer, balance_first ? &g_transfer : balance,
              min, max, &g_scratch_pool
          ),
          "and"
      );
      sum += arnm_roaring_cardinality(&result);
      const uint32_t written = arnm_roaring_page(&result, PAGE, PAGE, false, page);
      for (uint32_t i = 0; i < written; ++i) { sum = sum * 31u + page[i]; }
      if (getenv("LAYOUT_FREE_RESULT")) { arnm_roaring_free(&result, &g_scratch_pool); }
    }
  }
  const uint64_t nanos = (uint64_t)arnm_mono_timer_nanos(timer);
  g_sink = sum;
  return nanos;
}

/* the same filter as above, but asked in one walk instead of built: no result set, no blocks */
__attribute__((noinline)) static uint64_t time_balance_transfer_query(
    const address_sets *addresses, const uint32_t *keys, uint32_t count, uint32_t min, uint32_t max
) {
  uint64_t sum = 0;
  uint32_t page[PAGE];
  arnm_mono_timer timer;
  arnm_mono_timer_reset(&timer);
  for (uint32_t r = 0; r < 50u; ++r) {
    for (uint32_t k = 0; k < count; ++k) {
      const arnm_roaring_bitmap *both[2] = {&addresses[keys[k]].sets[0], &g_transfer};
      arnm_roaring_query query = {0};
      query.all = both;
      query.all_count = 2;
      query.min = min;
      query.max = max;
      uint64_t matches = 0;
      uint32_t written = 0;
      check(
          arnm_roaring_query_listing(&query, PAGE, PAGE, false, page, &written, &matches),
          "query listing"
      );
      sum += matches;
      for (uint32_t i = 0; i < written; ++i) { sum = sum * 31u + page[i]; }
    }
  }
  const uint64_t nanos = (uint64_t)arnm_mono_timer_nanos(timer);
  g_sink = sum;
  return nanos;
}

/*
 * What a page pays for the containers it passes over: the type set read from the front, from the
 * middle and from its end. A set of N values has N/65536 containers, each skipped by its stored
 * count alone -- the question is whether that walk is worth a cumulative count per container.
 */
__attribute__((noinline)) static void report_deep_page(const arnm_roaring_bitmap *set) {
  if (!set->cardinality) { return; }
  const uint32_t skips[3] = {
      0u, set->cardinality / 2u, set->cardinality > PAGE ? set->cardinality - PAGE : 0u
  };
  uint32_t page[PAGE];
  double nanos[3];
  for (int s = 0; s < 3; ++s) {
    uint64_t best = UINT64_MAX;
    for (int round = 0; round < 5; ++round) {
      arnm_mono_timer timer;
      arnm_mono_timer_reset(&timer);
      uint64_t sum = 0;
      for (uint32_t i = 0; i < 1000u; ++i) {
        sum += arnm_roaring_page(set, skips[s], PAGE, false, page) + page[0];
      }
      const uint64_t n = (uint64_t)arnm_mono_timer_nanos(timer);
      g_sink += sum;
      if (n < best) { best = n; }
    }
    nanos[s] = (double)best / 1000.0;
  }
  printf(
      "  page of 20 in a set of %u values (%u containers): from the front %.1f ns, from the "
      "middle %.1f ns, from the end %.1f ns\n",
      set->cardinality, set->count, nanos[0], nanos[1], nanos[2]
  );
}

/* reads every block of every address once, so the next pass starts with whatever the caches
   keep of 100 MiB and more -- nothing of the sample in particular */
static void evict(const address_sets *addresses, uint32_t count) {
  static uint8_t *scratch;
  const size_t bytes = 64u * 1024u * 1024u;
  if (!scratch) { scratch = (uint8_t *)malloc(bytes); }
  memset(scratch, (int)(count & 0xffu), bytes);
  g_sink = scratch[bytes / 2u] + addresses[0].involved_count;
}

static void run(const char *title, const bench_chain_data *data) {
  arnm_roaring_init(&g_transfer);
  printf("\n=== %s: %u transactions\n", title, data->count);
  map_lpn map;
  map_lpn_init(&map, 0, 75, PROTO_HASH_MIX32, 0x5eedull, NULL);
  uint32_t *ids = (uint32_t *)malloc((size_t)data->count * BENCH_TX_ADDRESS_MAX * sizeof(uint32_t));
  uint32_t r = 0;
  for (uint32_t i = 0; i < data->count; ++i) {
    for (uint8_t a = 0; a < data->records[i].address_count; ++a) {
      map_lpn_get_or_insert(&map, data->records[i].addresses[a].key, &ids[r++], NULL);
    }
  }
  const uint32_t address_count = map.count;
  map_lpn_free(&map);
  address_sets *addresses = (address_sets *)calloc(address_count, sizeof(address_sets));
  r = 0;
  for (uint32_t i = 0; i < data->count; ++i) {
    const bench_tx_record *record = &data->records[i];
    if (GRDT_TRANSACTION_TRANSFER == record->type) {
      check(arnm_roaring_add(&g_transfer, (uint32_t)record->tx_nr, &g_pool), "add");
    }
    for (uint8_t a = 0; a < record->address_count; ++a) {
      address_sets *sets = &addresses[ids[r++]];
      const uint8_t roles = record->addresses[a].roles;
      const uint32_t tx = (uint32_t)record->tx_nr;
      if (roles & BENCH_ADDRESS_BALANCE) {
        check(arnm_roaring_add(&sets->sets[0], tx, &g_pool), "add");
      }
      if (roles & BENCH_ADDRESS_SIGNER) {
        check(arnm_roaring_add(&sets->sets[1], tx, &g_pool), "add");
      }
      if (roles & BENCH_ADDRESS_OTHER) {
        check(arnm_roaring_add(&sets->sets[2], tx, &g_pool), "add");
      }
    }
  }
  free(ids);
  // the comparison: the union of each address as one plain array, each in its own pool block
  for (uint32_t i = 0; i < address_count; ++i) {
    address_sets *a = &addresses[i];
    const arnm_roaring_bitmap *list[3] = {&a->sets[0], &a->sets[1], &a->sets[2]};
    uint64_t count = 0;
    {
      arnm_roaring_query query = {0};
      query.any = list;
      query.any_count = 3;
      query.max = UINT32_MAX;
      check(arnm_roaring_query_cardinality(&query, &count), "count");
    }
    a->involved_count = (uint32_t)count;
    uint8_t log2 = 4;
    while (((uint64_t)1u << log2) < count * sizeof(uint32_t)) { ++log2; }
    uint8_t *block = NULL;
    check(arnm_graded_block_pool_alloc_log2(&g_array_pool, &block, log2), "array");
    a->involved = (uint32_t *)(void *)block;
    uint32_t written = 0;
    {
      arnm_roaring_query query = {0};
      query.any = list;
      query.any_count = 3;
      query.max = UINT32_MAX;
      check(
          arnm_roaring_query_page(&query, 0, (uint32_t)count, false, a->involved, &written), "fill"
      );
    }
  }

  // shape of the sets: all addresses, and the sample the benchmark reads
  const uint32_t sample_count = address_count < SAMPLE_KEYS ? address_count : SAMPLE_KEYS;
  uint32_t *sample = (uint32_t *)malloc(sample_count * sizeof(uint32_t));
  for (uint32_t i = 0; i < sample_count; ++i) {
    sample[i] = (uint32_t)((uint64_t)i * address_count / sample_count);
  }
  for (int which = 0; which < 2; ++which) {
    const uint32_t n = which ? sample_count : address_count;
    uint64_t values = 0, containers = 0, bytes = 0, sets = 0, involved = 0, max_containers = 0;
    uint64_t single_value_containers = 0, sparse_sets = 0;
    for (uint32_t k = 0; k < n; ++k) {
      const address_sets *a = &addresses[which ? sample[k] : k];
      involved += a->involved_count;
      for (int s = 0; s < 3; ++s) {
        const arnm_roaring_bitmap *set = &a->sets[s];
        if (!set->cardinality) { continue; }
        sets++;
        sparse_sets += 0u == set->count;
        values += set->cardinality;
        containers += set->count;
        if (set->count > max_containers) { max_containers = set->count; }
        bytes += set_bytes(set);
        for (uint32_t c = 0; c < set->count; ++c) {
          single_value_containers += 1u == set->containers[c].cardinality;
        }
      }
    }
    printf(
        "  %s: %u addresses, %.1f involved each; %llu non empty sets, %.1f values and %.1f "
        "containers "
        "per set (max %llu), %.2f values per container, %.0f%% of containers hold one value, "
        "%.1f bytes per value in blocks (plain uint32 array: 4), %.0f%% of sets sparse\n",
        which ? "sample" : "all   ", n, (double)involved / n, (unsigned long long)sets,
        (double)values / (double)sets, (double)containers / (double)sets,
        (unsigned long long)max_containers, containers ? (double)values / (double)containers : 0.0,
        containers ? 100.0 * (double)single_value_containers / (double)containers : 0.0,
        (double)bytes / (double)values, 100.0 * (double)sparse_sets / (double)sets
    );
  }

  // what "newest 20" of the union touches for the sample
  uint64_t keys = 0, touched = 0;
  for (uint32_t k = 0; k < sample_count; ++k) {
    uint32_t key_count, container_count;
    newest_footprint(&addresses[sample[k]], &key_count, &container_count);
    keys += key_count;
    touched += container_count;
  }
  printf(
      "  newest 20 of the sample, containers only: %.1f keys and %.1f containers read per "
      "address\n",
      (double)keys / sample_count, (double)touched / sample_count
  );

  // cold: once through the sample after the caches were filled with something else; warm: the
  // same sample again and again
  for (int round = 0; round < 3; ++round) {
    evict(addresses, (uint32_t)round);
    const uint64_t roaring_cold = time_roaring(addresses, sample, sample_count, 1);
    evict(addresses, (uint32_t)round + 1u);
    const uint64_t array_cold = time_array(addresses, sample, sample_count, 1);
    (void)time_roaring(addresses, sample, sample_count, 1);
    const uint64_t roaring_warm = time_roaring(addresses, sample, sample_count, WARM_REPEAT);
    (void)time_array(addresses, sample, sample_count, 1);
    const uint64_t array_warm = time_array(addresses, sample, sample_count, WARM_REPEAT);
    printf(
        "  newest 20, per address: roaring cold %6.1f ns, warm %6.1f ns | plain array cold %6.1f "
        "ns, warm %6.1f ns\n",
        (double)roaring_cold / sample_count, (double)roaring_warm / (sample_count * WARM_REPEAT),
        (double)array_cold / sample_count, (double)array_warm / (sample_count * WARM_REPEAT)
    );
  }

  // the busiest addresses by balance changes, their union counted in the middle half of the time
  // span -- the row "top20: involved, count in date range" of bench_tx_index_bitmap
  {
    uint32_t top[20];
    uint32_t top_count = 0;
    for (uint32_t i = 0; i < address_count; ++i) {
      uint32_t at = top_count < 20u ? top_count++ : 20u;
      while (at > 0 &&
             addresses[top[at - 1u]].sets[0].cardinality < addresses[i].sets[0].cardinality) {
        if (at < 20u) { top[at] = top[at - 1u]; }
        --at;
      }
      if (at < 20u) { top[at] = i; }
    }
    const int64_t first = data->records[0].confirmed_seconds;
    const int64_t last = data->records[data->count - 1u].confirmed_seconds;
    uint32_t i = 0, j = data->count;
    while (i < data->count && data->records[i].confirmed_seconds < first + (last - first) / 4) {
      ++i;
    }
    while (j > 0 && data->records[j - 1u].confirmed_seconds > first + 3 * (last - first) / 4) {
      --j;
    }
    const uint32_t min = (uint32_t)data->records[i].tx_nr;
    const uint32_t max = (uint32_t)data->records[j - 1u].tx_nr;
    for (uint32_t t = 0; t < 3 && t < top_count; ++t) {
      const address_sets *a = &addresses[top[t]];
      printf("  top %u: sets", t);
      for (int k = 0; k < 3; ++k) {
        const arnm_roaring_bitmap *set = &a->sets[k];
        printf(
            " [%u values, %s]", set->cardinality,
            !set->count               ? "sparse"
            : set->containers[0].kind ? "bitmap"
                                      : "array"
        );
      }
      printf("\n");
    }
    if (getenv("LAYOUT_PER_ADDRESS")) {
      for (uint32_t t = 0; t < top_count; ++t) {
        const address_sets *a = &addresses[top[t]];
        uint64_t best = UINT64_MAX;
        for (int round = 0; round < 5; ++round) {
          const uint64_t n = time_union_count(addresses, &top[t], 1, min, max);
          if (n < best) { best = n; }
        }
        uint64_t best_newest = UINT64_MAX;
        for (int round = 0; round < 5; ++round) {
          const uint64_t n = time_roaring(addresses, &top[t], 1, 50);
          if (n < best_newest) { best_newest = n; }
        }
        uint64_t best_and = UINT64_MAX;
        for (int round = 0; round < 5; ++round) {
          const uint64_t n = time_balance_transfer(addresses, &top[t], 1, min, max);
          if (n < best_and) { best_and = n; }
        }
        printf(
            "    address %2u: count %7.1f ns, newest 20 %6.1f ns, and %6.1f ns |", t,
            (double)best / 50.0, (double)best_newest / 50.0, (double)best_and / 50.0
        );
        for (int k = 0; k < 3; ++k) {
          const arnm_roaring_bitmap *set = &a->sets[k];
          printf(
              " %6u %s", set->cardinality,
              !set->cardinality                                               ? "empty "
              : (!set->count && set->cardinality <= 1024u && set->containers) ? "sparse"
              : set->containers[0].kind                                       ? "bitmap"
                                                                              : "array "
          );
        }
        printf("\n");
      }
    }
    report_deep_page(&g_transfer);
    const uint64_t nanos = time_union_count(addresses, top, top_count, min, max);
    printf(
        "  top20 union count in [%u, %u]: %.1f ns per query\n", min, max,
        (double)nanos / (top_count * 50.0)
    );
    uint64_t built = UINT64_MAX, asked = UINT64_MAX, newest = UINT64_MAX;
    for (int round = 0; round < 5; ++round) {
      uint64_t n = time_balance_transfer(addresses, top, top_count, min, max);
      if (n < built) { built = n; }
      n = time_balance_transfer_query(addresses, top, top_count, min, max);
      if (n < asked) { asked = n; }
      n = time_roaring(addresses, top, top_count, 50);
      if (n < newest) { newest = n; }
    }
    printf(
        "  top20 balance AND TRANSFER: built %.1f ns, asked %.1f ns | newest 20: %.1f ns\n",
        (double)built / (top_count * 50.0), (double)asked / (top_count * 50.0),
        (double)newest / (top_count * 50.0)
    );
  }

  for (uint32_t i = 0; i < address_count; ++i) {
    for (int s = 0; s < 3; ++s) { arnm_roaring_free(&addresses[i].sets[s], &g_pool); }
  }
  free(addresses);
  free(sample);
  arnm_roaring_free(&g_transfer, &g_pool);
  arnm_graded_block_pool_reset(&g_pool);
  arnm_graded_block_pool_reset(&g_array_pool);
}

int main(int argc, char **argv) {
  arnm_graded_block_pool_options options = {0};
  check(arnm_graded_block_pool_init(&g_pool, &options, NULL), "pool");
  options = (arnm_graded_block_pool_options){0};
  check(arnm_graded_block_pool_init(&g_array_pool, &options, NULL), "pool");
  options = (arnm_graded_block_pool_options){0};
  check(arnm_graded_block_pool_init(&g_scratch_pool, &options, NULL), "pool");
  const char *path = bench_chain_data_path(argc, argv);
  if (path) {
    bench_chain_data data;
    check(bench_chain_data_load(&data, path, bench_default_community_uuid), "load");
    run(path, &data);
    bench_chain_data_free(&data);
  }
  bench_chain_data synthetic;
  check(bench_chain_data_synthetic(&synthetic, 2000000u, 100000u, 42u), "synthetic");
  run("synthetic 2M tx / 100k addresses", &synthetic);
  bench_chain_data_free(&synthetic);
  arnm_graded_block_pool_release(&g_pool, NULL);
  arnm_graded_block_pool_release(&g_array_pool, NULL);
  arnm_graded_block_pool_release(&g_scratch_pool, NULL);
  return 0;
}
