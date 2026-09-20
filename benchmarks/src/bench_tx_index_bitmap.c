#include "arnm/arena.h"
#include "arnm/mono_timer.h"
#include "bench_chain_data.h"
#include "bench_report.h"
#include "gradido_blockchain_core/types/transaction.h"
#include "proto/maps.h"
#include "proto/proto_common.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The transaction sets of the index, one body over two bitmap backends.
 *
 * The index is the one TransactionsIndexRoaringBitmaps keeps: per address a set of the
 * transactions that changed its balance, that it signed and (lazily) where it was otherwise
 * named; one set per transaction type; one per foreign coin community. The public key -> id map
 * is built before the clock starts, so these rows time the sets only.
 *
 * A query is aggregateTransactions: intersect the sets a filter names, smallest first, restrict
 * to a transaction number range (what a date range becomes), subtract the foreign coin set,
 * then count or cut a page. Both backends are used the way each is best at it rather than the
 * way the C++ happens to do it:
 *   - no range bitmap is ever built; a single set is read through [min, max] as it stands,
 *     with range_cardinality and select
 *   - CRoaring intersects whole sets and applies the range when reading the result -- it has
 *     no ranged AND -- while the append-only prototype applies the range while intersecting
 *
 * Built once per backend: BENCH_BITMAP_CROARING (roaring64, CRoaring's default x64 paths,
 * AVX-512 off) and the same with ROARING_DISABLE_X64 (scalar), BENCH_BITMAP_CROARING32 (the
 * flat 32 bit roaring_bitmap_t), and BENCH_BITMAP_ARNM_ROARING. The checksum at the end of each
 * query row has to be the same in all of them.
 *
 * Usage: bench_tx_index_bitmap_* [chain file]   (or GRD_BENCH_CHAIN_DATA)
 */

#define SAMPLE_KEYS 1000u
#define TOP_KEYS 20u
#define TOP_REPEAT 50u
#define WIDE_REPEAT 200u
#define PAGE_SIZE 20u
#define SYNTHETIC_TX 2000000u
#define SYNTHETIC_KEYS 100000u

/* ------------------------------------------------------------------ backend */

#if defined(BENCH_BITMAP_CROARING)
#include "croaring/roaring.h"

#if defined(ROARING_DISABLE_X64)
#define BACKEND_NAME "CRoaring 4.6.1 scalar (ROARING_DISABLE_X64)"
#else
#define BACKEND_NAME "CRoaring 4.6.1 (x64 SIMD dispatch, AVX-512 off)"
#endif

typedef roaring64_bitmap_t *bm_set;

typedef struct bm_result {
  roaring64_bitmap_t *owned; /**< set a query materialized, freed when the query ends */
  const roaring64_bitmap_t *set;
  uint64_t min;
  uint64_t max;
  bool full_range; /**< every transaction in [min, max]: no set at all */
} bm_result;

static void backend_init(void) {
  roaring_memory_t hook = {
      proto_counted_malloc, proto_counted_realloc,        proto_counted_calloc,
      proto_counted_free,   proto_counted_aligned_malloc, proto_counted_aligned_free,
  };
  roaring_init_memory_hook(hook);
}

static inline void set_add(bm_set *set, uint64_t value) {
  if (!*set) { *set = roaring64_bitmap_create(); }
  roaring64_bitmap_add(*set, value);
}

static inline uint64_t set_cardinality(const bm_set *set) {
  return *set ? roaring64_bitmap_get_cardinality(*set) : 0u;
}

static inline void set_free(bm_set *set) {
  if (*set) { roaring64_bitmap_free(*set); }
  *set = NULL;
}

static void backend_memory(char *buffer, size_t size) {
  proto_counted_stats stats = proto_counted_get_stats();
  // requested bytes only: glibc adds at least 8 bytes of header and rounds to 16 per block
  snprintf(
      buffer, size, "malloc live %.1f MiB in %llu blocks, peak %.1f MiB",
      (double)stats.current_bytes / 1048576.0, (unsigned long long)stats.current_blocks,
      (double)stats.peak_bytes / 1048576.0
  );
}

static void query_begin(void) {
}

static void query_end(bm_result *result) {
  if (result->owned) { roaring64_bitmap_free(result->owned); }
  result->owned = NULL;
}

/** Union of up to three sets, as a set of its own (InvolvedPublicKey). */
static const bm_set *set_union3(
    const bm_set *a, const bm_set *b, const bm_set *c, bm_result *keep
) {
  static bm_set scratch;
  if (!scratch) { scratch = roaring64_bitmap_create(); }
  roaring64_bitmap_clear(scratch);
  if (*a) { roaring64_bitmap_or_inplace(scratch, *a); }
  if (*b) { roaring64_bitmap_or_inplace(scratch, *b); }
  if (*c) { roaring64_bitmap_or_inplace(scratch, *c); }
  (void)keep;
  return &scratch;
}

static void aggregate(
    bm_result *out,
    const bm_set **sets,
    uint32_t count,
    const bm_set *exclude,
    uint64_t min,
    uint64_t max
) {
  memset(out, 0, sizeof(*out));
  out->min = min;
  out->max = max;
  if (!count) {
    out->full_range = true;
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    // a set that was never created is empty, and so is every intersection with it
    if (!*sets[i]) { return; }
  }
  for (uint32_t i = 1; i < count; ++i) {
    for (uint32_t j = i; j > 0 && set_cardinality(sets[j]) < set_cardinality(sets[j - 1u]); --j) {
      const bm_set *swap = sets[j];
      sets[j] = sets[j - 1u];
      sets[j - 1u] = swap;
    }
  }
  if (1u == count) {
    if (exclude && *exclude) {
      out->owned = roaring64_bitmap_andnot(*sets[0], *exclude);
      out->set = out->owned;
    } else {
      out->set = *sets[0];
    }
    return;
  }
  out->owned = roaring64_bitmap_and(*sets[0], *sets[1]);
  for (uint32_t i = 2; i < count; ++i) { roaring64_bitmap_and_inplace(out->owned, *sets[i]); }
  if (exclude && *exclude) { roaring64_bitmap_andnot_inplace(out->owned, *exclude); }
  out->set = out->owned;
}

/** Values of @p set below @p value. */
static inline uint64_t rank_below(const roaring64_bitmap_t *set, uint64_t value) {
  return value ? roaring64_bitmap_range_cardinality(set, 0, value) : 0u;
}

static uint64_t result_count(const bm_result *result) {
  if (result->full_range) { return result->max - result->min + 1u; }
  if (!result->set) { return 0u; }
  uint64_t below = rank_below(result->set, result->min);
  uint64_t through = result->max == UINT64_MAX ? roaring64_bitmap_get_cardinality(result->set)
                                               : rank_below(result->set, result->max + 1u);
  return through - below;
}

static uint32_t result_page(
    const bm_result *result, uint64_t skip, uint32_t size, bool descending, uint64_t *out
) {
  uint64_t count = result_count(result);
  if (skip >= count) { return 0u; }
  uint32_t take = (uint32_t)((count - skip) < size ? (count - skip) : size);
  if (result->full_range) {
    for (uint32_t i = 0; i < take; ++i) {
      out[i] = descending ? result->max - skip - i : result->min + skip + i;
    }
    return take;
  }
  uint64_t base = rank_below(result->set, result->min);
  uint64_t first_rank = descending ? base + count - 1u - skip : base + skip;
  uint64_t first_value = 0;
  roaring64_bitmap_select(result->set, first_rank, &first_value);
  roaring64_iterator_t *it = roaring64_iterator_create(result->set);
  roaring64_iterator_move_equalorlarger(it, first_value);
  uint32_t written = 0;
  if (!descending) {
    written = (uint32_t)roaring64_iterator_read(it, out, take);
  } else {
    while (written < take && roaring64_iterator_has_value(it)) {
      out[written++] = roaring64_iterator_value(it);
      roaring64_iterator_previous(it);
    }
  }
  roaring64_iterator_free(it);
  return written;
}

#elif defined(BENCH_BITMAP_CROARING32)
#include "croaring/roaring.h"

/*
 * CRoaring's 32 bit bitmap: a flat sorted array of containers instead of roaring64's ART.
 * Transaction numbers are stored as they are; every chain here stays below 2^32, and a real
 * index would store tx - first tx of the block to keep that true.
 */
#if defined(ROARING_DISABLE_X64)
#define BACKEND_NAME "CRoaring 4.6.1 32 bit, scalar (ROARING_DISABLE_X64)"
#else
#define BACKEND_NAME "CRoaring 4.6.1 32 bit (x64 SIMD dispatch, AVX-512 off)"
#endif

typedef roaring_bitmap_t *bm_set;

typedef struct bm_result {
  roaring_bitmap_t *owned;
  const roaring_bitmap_t *set;
  uint64_t min;
  uint64_t max;
  bool full_range;
} bm_result;

static void backend_init(void) {
  roaring_memory_t hook = {
      proto_counted_malloc, proto_counted_realloc,        proto_counted_calloc,
      proto_counted_free,   proto_counted_aligned_malloc, proto_counted_aligned_free,
  };
  roaring_init_memory_hook(hook);
}

static inline void set_add(bm_set *set, uint64_t value) {
  if (value > UINT32_MAX) {
    fprintf(stderr, "transaction number past 32 bits\n");
    exit(1);
  }
  if (!*set) { *set = roaring_bitmap_create(); }
  roaring_bitmap_add(*set, (uint32_t)value);
}

static inline uint64_t set_cardinality(const bm_set *set) {
  return *set ? roaring_bitmap_get_cardinality(*set) : 0u;
}

static inline void set_free(bm_set *set) {
  if (*set) { roaring_bitmap_free(*set); }
  *set = NULL;
}

static void backend_memory(char *buffer, size_t size) {
  proto_counted_stats stats = proto_counted_get_stats();
  // requested bytes only: glibc adds at least 8 bytes of header and rounds to 16 per block
  snprintf(
      buffer, size, "malloc live %.1f MiB in %llu blocks, peak %.1f MiB",
      (double)stats.current_bytes / 1048576.0, (unsigned long long)stats.current_blocks,
      (double)stats.peak_bytes / 1048576.0
  );
}

static void query_begin(void) {
}

static void query_end(bm_result *result) {
  if (result->owned) { roaring_bitmap_free(result->owned); }
  result->owned = NULL;
}

static const bm_set *set_union3(
    const bm_set *a, const bm_set *b, const bm_set *c, bm_result *keep
) {
  static bm_set scratch;
  if (!scratch) { scratch = roaring_bitmap_create(); }
  roaring_bitmap_clear(scratch);
  if (*a) { roaring_bitmap_or_inplace(scratch, *a); }
  if (*b) { roaring_bitmap_or_inplace(scratch, *b); }
  if (*c) { roaring_bitmap_or_inplace(scratch, *c); }
  (void)keep;
  return &scratch;
}

static void aggregate(
    bm_result *out,
    const bm_set **sets,
    uint32_t count,
    const bm_set *exclude,
    uint64_t min,
    uint64_t max
) {
  memset(out, 0, sizeof(*out));
  out->min = min;
  out->max = max;
  if (!count) {
    out->full_range = true;
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    if (!*sets[i]) { return; }
  }
  for (uint32_t i = 1; i < count; ++i) {
    for (uint32_t j = i; j > 0 && set_cardinality(sets[j]) < set_cardinality(sets[j - 1u]); --j) {
      const bm_set *swap = sets[j];
      sets[j] = sets[j - 1u];
      sets[j - 1u] = swap;
    }
  }
  if (1u == count) {
    if (exclude && *exclude) {
      out->owned = roaring_bitmap_andnot(*sets[0], *exclude);
      out->set = out->owned;
    } else {
      out->set = *sets[0];
    }
    return;
  }
  out->owned = roaring_bitmap_and(*sets[0], *sets[1]);
  for (uint32_t i = 2; i < count; ++i) { roaring_bitmap_and_inplace(out->owned, *sets[i]); }
  if (exclude && *exclude) { roaring_bitmap_andnot_inplace(out->owned, *exclude); }
  out->set = out->owned;
}

static uint64_t result_count(const bm_result *result) {
  if (result->full_range) { return result->max - result->min + 1u; }
  if (!result->set) { return 0u; }
  uint64_t end = result->max >= UINT32_MAX ? (uint64_t)UINT32_MAX + 1u : result->max + 1u;
  return roaring_bitmap_range_cardinality(result->set, result->min, end);
}

static uint32_t result_page(
    const bm_result *result, uint64_t skip, uint32_t size, bool descending, uint64_t *out
) {
  uint64_t count = result_count(result);
  if (skip >= count) { return 0u; }
  uint32_t take = (uint32_t)((count - skip) < size ? (count - skip) : size);
  if (result->full_range) {
    for (uint32_t i = 0; i < take; ++i) {
      out[i] = descending ? result->max - skip - i : result->min + skip + i;
    }
    return take;
  }
  uint64_t base = result->min ? roaring_bitmap_range_cardinality(result->set, 0, result->min) : 0u;
  uint64_t first_rank = descending ? base + count - 1u - skip : base + skip;
  uint32_t first_value = 0;
  roaring_bitmap_select(result->set, (uint32_t)first_rank, &first_value);
  // the 32 bit iterator lives on the stack, no allocation per page
  roaring_uint32_iterator_t it;
  roaring_iterator_init(result->set, &it);
  roaring_uint32_iterator_move_equalorlarger(&it, first_value);
  uint32_t written = 0;
  while (written < take && it.has_value) {
    out[written++] = it.current_value;
    if (descending) {
      roaring_uint32_iterator_previous(&it);
    } else {
      roaring_uint32_iterator_advance(&it);
    }
  }
  return written;
}

/*
 * The newest page of an address's union without building it, the way CRoaring can
 * do it too -- one iterator per set from its last value backwards, merged, stopped at the page.
 * The count has no such way in CRoaring and keeps the built union.
 */
#define BACKEND_HAS_UNION_PAGE 1

static uint32_t union3_newest(
    const bm_set *a, const bm_set *b, const bm_set *c, uint32_t size, uint64_t *out
) {
  const bm_set *sets[3] = {a, b, c};
  roaring_uint32_iterator_t iterators[3];
  uint32_t live = 0;
  for (uint32_t i = 0; i < 3; ++i) {
    if (!*sets[i]) { continue; }
    roaring_iterator_init_last(*sets[i], &iterators[live]);
    if (iterators[live].has_value) { ++live; }
  }
  uint32_t written = 0;
  while (written < size && live) {
    uint32_t largest = 0;
    for (uint32_t i = 0; i < live; ++i) {
      if (iterators[i].current_value > largest) { largest = iterators[i].current_value; }
    }
    out[written++] = largest;
    // every iterator standing on it moves on; one that runs out is dropped
    for (uint32_t i = 0; i < live;) {
      if (iterators[i].current_value == largest &&
          !roaring_uint32_iterator_previous(&iterators[i])) {
        iterators[i] = iterators[--live];
        continue;
      }
      ++i;
    }
  }
  return written;
}

#elif defined(BENCH_BITMAP_ARNM_ROARING)
#include "arnm/graded_block_pool.h"
#include "arnm/multi_arena.h"
#include "arnm/roaring_bitmap.h"
#include "arnm/roaring_ops.h"
#include "arnm/roaring_query.h"

#define BACKEND_NAME "arnm/roaring_bitmap.h (uint32, graded block pool)"

/*
 * The sets the transaction index is built on. Values are uint32_t, so every uint64_t range of
 * the shared body is cut to UINT32_MAX here; the chains measured stay far below it, and the
 * index keeps a uint64_t base beside its sets for the rest. The scratch pool is reset before
 * each query, the way a query arena would be.
 */

typedef arnm_roaring_bitmap bm_set;

typedef struct bm_result {
  arnm_roaring_bitmap owned;
  const arnm_roaring_bitmap *set;
  uint64_t min;
  uint64_t max;
  bool full_range;
} bm_result;

static arnm_graded_block_pool g_index_pool;
static arnm_graded_block_pool g_scratch_pool;

static inline uint32_t to_u32(uint64_t value) {
  return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static void check(arnm_result result) {
  if (ARNM_SUCCESS != result) {
    fprintf(stderr, "roaring operation failed: %d\n", (int)result);
    exit(1);
  }
}

static void backend_init(void) {
  arnm_graded_block_pool_options options = {0};
  check(arnm_graded_block_pool_init(&g_index_pool, &options, NULL));
  options = (arnm_graded_block_pool_options){0};
  check(arnm_graded_block_pool_init(&g_scratch_pool, &options, NULL));
}

static inline void set_add(bm_set *set, uint64_t value) {
  if (value > UINT32_MAX) {
    fprintf(stderr, "transaction number past uint32_t\n");
    exit(1);
  }
  check(arnm_roaring_add(set, (uint32_t)value, &g_index_pool));
}

static inline uint64_t set_cardinality(const bm_set *set) {
  return arnm_roaring_cardinality(set);
}

static inline void set_free(bm_set *set) {
  arnm_roaring_free(set, &g_index_pool);
}

static void backend_memory(char *buffer, size_t size) {
  arnm_multi_arena_stats chain = {0};
  (void)arnm_multi_arena_measure(g_index_pool.source, &chain);
  snprintf(
      buffer, size, "blocks live %.1f MiB, free lists %.1f MiB, arenas reserved %.1f MiB",
      (double)g_index_pool.lent_bytes / 1048576.0, (double)g_index_pool.cached_bytes / 1048576.0,
      (double)chain.reserved / 1048576.0
  );
}

static void query_begin(void) {
  arnm_graded_block_pool_reset(&g_scratch_pool);
}

static void query_end(bm_result *result) {
  (void)result; // the scratch pool is reset by the next query_begin
}

static const bm_set *set_union3(
    const bm_set *a, const bm_set *b, const bm_set *c, bm_result *keep
) {
  arnm_roaring_bitmap first;
  arnm_roaring_init(&first);
  arnm_roaring_init(&keep->owned);
  check(arnm_roaring_or(&first, a, b, 0, UINT32_MAX, &g_scratch_pool));
  check(arnm_roaring_or(&keep->owned, &first, c, 0, UINT32_MAX, &g_scratch_pool));
  return &keep->owned;
}

static void aggregate(
    bm_result *out,
    const bm_set **sets,
    uint32_t count,
    const bm_set *exclude,
    uint64_t min,
    uint64_t max
) {
  arnm_roaring_init(&out->owned);
  out->set = NULL;
  out->min = min;
  out->max = max;
  out->full_range = false;
  if (!count) {
    out->full_range = true;
    return;
  }
  for (uint32_t i = 1; i < count; ++i) {
    for (uint32_t j = i; j > 0 && set_cardinality(sets[j]) < set_cardinality(sets[j - 1u]); --j) {
      const bm_set *swap = sets[j];
      sets[j] = sets[j - 1u];
      sets[j - 1u] = swap;
    }
  }
  if (1u == count && !(exclude && exclude->cardinality)) {
    out->set = sets[0];
    return;
  }
  // the range goes into the first operation; everything after it is already inside
  const uint32_t low = to_u32(min);
  const uint32_t high = to_u32(max);
  arnm_roaring_bitmap current;
  arnm_roaring_init(&current);
  if (1u == count) {
    check(arnm_roaring_andnot(&current, sets[0], exclude, low, high, &g_scratch_pool));
  } else {
    check(arnm_roaring_and(&current, sets[0], sets[1], low, high, &g_scratch_pool));
    for (uint32_t i = 2; i < count; ++i) {
      arnm_roaring_bitmap next;
      arnm_roaring_init(&next);
      check(arnm_roaring_and(&next, &current, sets[i], 0, UINT32_MAX, &g_scratch_pool));
      current = next;
    }
    if (exclude && exclude->cardinality) {
      arnm_roaring_bitmap next;
      arnm_roaring_init(&next);
      check(arnm_roaring_andnot(&next, &current, exclude, 0, UINT32_MAX, &g_scratch_pool));
      current = next;
    }
  }
  out->owned = current;
  out->set = &out->owned;
  out->min = 0;
  out->max = UINT64_MAX;
}

static uint64_t result_count(const bm_result *result) {
  if (result->full_range) { return result->max - result->min + 1u; }
  if (0u == result->min && UINT32_MAX <= result->max) { return result->set->cardinality; }
  return arnm_roaring_range_cardinality(result->set, to_u32(result->min), to_u32(result->max));
}

static uint32_t result_page(
    const bm_result *result, uint64_t skip, uint32_t size, bool descending, uint64_t *out
) {
  uint64_t count = result_count(result);
  if (skip >= count) { return 0u; }
  uint32_t take = (uint32_t)((count - skip) < size ? (count - skip) : size);
  if (result->full_range) {
    for (uint32_t i = 0; i < take; ++i) {
      out[i] = descending ? result->max - skip - i : result->min + skip + i;
    }
    return take;
  }
  // a range view skips what lies outside it on the side the page starts from
  uint64_t outside = 0;
  if (!descending && result->min) {
    outside = arnm_roaring_range_cardinality(result->set, 0, to_u32(result->min - 1u));
  } else if (descending && result->max < UINT32_MAX) {
    outside = arnm_roaring_range_cardinality(result->set, to_u32(result->max + 1u), UINT32_MAX);
  }
  uint32_t values[PAGE_SIZE];
  uint32_t written = 0;
  while (written < take) {
    const uint32_t chunk = take - written < PAGE_SIZE ? take - written : PAGE_SIZE;
    const uint32_t got = arnm_roaring_page(
        result->set, (uint32_t)(skip + outside + written), chunk, descending, values
    );
    for (uint32_t i = 0; i < got; ++i) { out[written + i] = values[i]; }
    written += got;
    if (got < chunk) { break; }
  }
  return written;
}

/* the union of an address's three sets is read in place, never built */
#define BACKEND_HAS_UNION_PAGE 1
#define BACKEND_HAS_UNION_COUNT 1

static uint64_t union3_count(
    const bm_set *a, const bm_set *b, const bm_set *c, uint64_t min, uint64_t max
) {
  const arnm_roaring_bitmap *sets[3] = {a, b, c};
  arnm_roaring_query query = {0};
  query.any = sets;
  query.any_count = 3;
  query.min = to_u32(min);
  query.max = to_u32(max);
  uint64_t count = 0;
  check(arnm_roaring_query_cardinality(&query, &count));
  return count;
}

static uint32_t union3_newest(
    const bm_set *a, const bm_set *b, const bm_set *c, uint32_t size, uint64_t *out
) {
  const arnm_roaring_bitmap *sets[3] = {a, b, c};
  arnm_roaring_query query = {0};
  query.any = sets;
  query.any_count = 3;
  query.max = UINT32_MAX;
  uint32_t values[PAGE_SIZE];
  uint32_t written = 0;
  check(arnm_roaring_query_page(&query, 0, size, true, values, &written));
  for (uint32_t i = 0; i < written; ++i) { out[i] = values[i]; }
  return written;
}

#else
#error "define BENCH_BITMAP_CROARING, BENCH_BITMAP_CROARING32 or BENCH_BITMAP_ARNM_ROARING"
#endif

/* ------------------------------------------------------------------ index */

typedef struct address_sets {
  bm_set balance;
  bm_set signer;
  bm_set other;
} address_sets;

typedef struct tx_index {
  address_sets *addresses;
  uint32_t address_count;
  bm_set per_type[GRDT_TRANSACTION_COUNT];
  bm_set foreign_coin[BENCH_COMMUNITY_MAX];
  uint64_t min_tx;
  uint64_t max_tx;
} tx_index;

typedef struct prepared {
  const bench_chain_data *data;
  uint32_t *address_ids; /**< per record, per address: the id the map gave its key */
  uint32_t *record_offsets;
  uint32_t unique_keys;
} prepared;

static void prepare(prepared *p, const bench_chain_data *data) {
  memset(p, 0, sizeof(*p));
  p->data = data;
  uint32_t references = 0;
  p->record_offsets = (uint32_t *)malloc(((size_t)data->count + 1u) * sizeof(uint32_t));
  for (uint32_t i = 0; i < data->count; ++i) {
    p->record_offsets[i] = references;
    references += data->records[i].address_count;
  }
  p->record_offsets[data->count] = references;
  p->address_ids = (uint32_t *)malloc((size_t)references * sizeof(uint32_t) + 1u);
  map_lpn map;
  map_lpn_init(&map, 0, 75, PROTO_HASH_MIX32, 0x5eedull, NULL);
  uint32_t r = 0;
  for (uint32_t i = 0; i < data->count; ++i) {
    for (uint8_t a = 0; a < data->records[i].address_count; ++a) {
      map_lpn_get_or_insert(&map, data->records[i].addresses[a].key, &p->address_ids[r++], NULL);
    }
  }
  p->unique_keys = map.count;
  map_lpn_free(&map);
}

static void prepared_free(prepared *p) {
  free(p->address_ids);
  free(p->record_offsets);
}

static void index_build(tx_index *index, const prepared *p) {
  memset(index, 0, sizeof(*index));
  index->address_count = p->unique_keys;
  index->addresses =
      (address_sets *)calloc(p->unique_keys ? p->unique_keys : 1u, sizeof(address_sets));
  const bench_chain_data *data = p->data;
  if (data->count) {
    index->min_tx = data->records[0].tx_nr;
    index->max_tx = data->records[data->count - 1u].tx_nr;
  }
  for (uint32_t i = 0; i < data->count; ++i) {
    const bench_tx_record *record = &data->records[i];
    uint32_t offset = p->record_offsets[i];
    for (uint8_t a = 0; a < record->address_count; ++a) {
      address_sets *sets = &index->addresses[p->address_ids[offset + a]];
      uint8_t roles = record->addresses[a].roles;
      if (roles & BENCH_ADDRESS_BALANCE) { set_add(&sets->balance, record->tx_nr); }
      if (roles & BENCH_ADDRESS_SIGNER) { set_add(&sets->signer, record->tx_nr); }
      if (roles & BENCH_ADDRESS_OTHER) { set_add(&sets->other, record->tx_nr); }
    }
    if (record->type < GRDT_TRANSACTION_COUNT) {
      set_add(&index->per_type[record->type], record->tx_nr);
    }
    for (uint8_t c = 0; c < record->foreign_coin_count; ++c) {
      set_add(&index->foreign_coin[record->foreign_coin[c]], record->tx_nr);
    }
  }
}

static void index_free(tx_index *index) {
  for (uint32_t i = 0; i < index->address_count; ++i) {
    set_free(&index->addresses[i].balance);
    set_free(&index->addresses[i].signer);
    set_free(&index->addresses[i].other);
  }
  for (uint32_t t = 0; t < GRDT_TRANSACTION_COUNT; ++t) { set_free(&index->per_type[t]); }
  for (uint32_t c = 0; c < BENCH_COMMUNITY_MAX; ++c) { set_free(&index->foreign_coin[c]); }
  free(index->addresses);
}

/* ------------------------------------------------------------------ queries */

typedef struct checksum {
  uint64_t counts;
  uint64_t values;
} checksum;

static inline void checksum_page(checksum *sum, const uint64_t *values, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) { sum->values = sum->values * 31u + values[i]; }
  sum->counts += count;
}

typedef struct query_context {
  const tx_index *index;
  uint64_t range_min; /**< transaction numbers the middle half of the chain's time covers */
  uint64_t range_max;
  const uint32_t *keys;
  uint32_t key_count;
  uint32_t repeat;
} query_context;

static void print_query(const char *name, uint64_t nanos, uint32_t queries, const checksum *sum) {
  char total[BENCH_STRING_BUFFER_SIZE];
  char per_query[BENCH_STRING_BUFFER_SIZE];
  snprintf(total, sizeof(total), "%.2f ms", (double)nanos / 1e6);
  bench_per_step_string(per_query, sizeof(per_query), queries ? (double)nanos / queries : 0.0);
  printf(
      "  %-44s %11s %11s/query  check %llu:%016llx\n", name, total, per_query,
      (unsigned long long)sum->counts, (unsigned long long)sum->values
  );
}

typedef void (*query_fn)(const query_context *ctx, uint32_t key, checksum *sum);

static void q_involved_newest(const query_context *ctx, uint32_t key, checksum *sum) {
  const address_sets *a = &ctx->index->addresses[key];
#if defined(BACKEND_HAS_UNION_PAGE)
  {
    uint64_t newest[PAGE_SIZE];
    checksum_page(
        sum, newest, union3_newest(&a->balance, &a->signer, &a->other, PAGE_SIZE, newest)
    );
    return;
  }
#endif
  bm_result keep;
  memset(&keep, 0, sizeof(keep));
  query_begin();
  const bm_set *involved = set_union3(&a->balance, &a->signer, &a->other, &keep);
  const bm_set *sets[1] = {involved};
  bm_result result;
  aggregate(&result, sets, 1, NULL, 0, UINT64_MAX);
  uint64_t page[PAGE_SIZE];
  checksum_page(sum, page, result_page(&result, 0, PAGE_SIZE, true, page));
  query_end(&result);
}

static void q_involved_count_range(const query_context *ctx, uint32_t key, checksum *sum) {
  const address_sets *a = &ctx->index->addresses[key];
#if defined(BACKEND_HAS_UNION_COUNT)
  sum->counts += union3_count(&a->balance, &a->signer, &a->other, ctx->range_min, ctx->range_max);
  return;
#endif
  bm_result keep;
  memset(&keep, 0, sizeof(keep));
  query_begin();
  const bm_set *involved = set_union3(&a->balance, &a->signer, &a->other, &keep);
  const bm_set *sets[1] = {involved};
  bm_result result;
  aggregate(&result, sets, 1, NULL, ctx->range_min, ctx->range_max);
  sum->counts += result_count(&result);
  query_end(&result);
}

static void q_balance_transfer_range_page(const query_context *ctx, uint32_t key, checksum *sum) {
  const bm_set *sets[2] = {
      &ctx->index->addresses[key].balance, &ctx->index->per_type[GRDT_TRANSACTION_TRANSFER]
  };
  query_begin();
  bm_result result;
  aggregate(&result, sets, 2, NULL, ctx->range_min, ctx->range_max);
  sum->counts += result_count(&result);
  uint64_t page[PAGE_SIZE];
  checksum_page(sum, page, result_page(&result, PAGE_SIZE, PAGE_SIZE, false, page));
  query_end(&result);
}

static void q_balance_count_all(const query_context *ctx, uint32_t key, checksum *sum) {
  const bm_set *sets[1] = {&ctx->index->addresses[key].balance};
  query_begin();
  bm_result result;
  aggregate(&result, sets, 1, NULL, 0, UINT64_MAX);
  sum->counts += result_count(&result);
  query_end(&result);
}

static void q_transfer_range_newest(const query_context *ctx, uint32_t key, checksum *sum) {
  (void)key;
  const bm_set *sets[1] = {&ctx->index->per_type[GRDT_TRANSACTION_TRANSFER]};
  query_begin();
  bm_result result;
  aggregate(&result, sets, 1, NULL, ctx->range_min, ctx->range_max);
  sum->counts += result_count(&result);
  uint64_t page[PAGE_SIZE];
  checksum_page(sum, page, result_page(&result, 0, PAGE_SIZE, true, page));
  query_end(&result);
}

static void q_creation_without_foreign(const query_context *ctx, uint32_t key, checksum *sum) {
  (void)key;
  const bm_set *sets[1] = {&ctx->index->per_type[GRDT_TRANSACTION_CREATION]};
  query_begin();
  bm_result result;
  aggregate(&result, sets, 1, &ctx->index->foreign_coin[1], ctx->range_min, ctx->range_max);
  sum->counts += result_count(&result);
  query_end(&result);
}

static void q_transfer_and_deferred_range(const query_context *ctx, uint32_t key, checksum *sum) {
  (void)key;
  // two type sets never intersect; this is the wide AND of two large sets, answer 0
  const bm_set *sets[2] = {
      &ctx->index->per_type[GRDT_TRANSACTION_TRANSFER],
      &ctx->index->per_type[GRDT_TRANSACTION_DEFERRED_TRANSFER]
  };
  query_begin();
  bm_result result;
  aggregate(&result, sets, 2, NULL, ctx->range_min, ctx->range_max);
  sum->counts += result_count(&result);
  query_end(&result);
}

static void run_query(const char *name, query_fn fn, const query_context *ctx) {
  checksum sum = {0, 0};
  arnm_mono_timer timer;
  arnm_mono_timer_reset(&timer);
  for (uint32_t r = 0; r < ctx->repeat; ++r) {
    for (uint32_t k = 0; k < ctx->key_count; ++k) { fn(ctx, ctx->keys[k], &sum); }
  }
  print_query(name, (uint64_t)arnm_mono_timer_nanos(timer), ctx->repeat * ctx->key_count, &sum);
}

static const tx_index *g_sort_index;
static int compare_keys_by_balance(const void *a, const void *b) {
  uint64_t ca = set_cardinality(&g_sort_index->addresses[*(const uint32_t *)a].balance);
  uint64_t cb = set_cardinality(&g_sort_index->addresses[*(const uint32_t *)b].balance);
  return ca < cb ? 1 : (ca > cb ? -1 : 0);
}

static void run_dataset(const char *title, const bench_chain_data *data) {
  printf("\n=== %s: %u transactions\n", title, data->count);
  prepared p;
  prepare(&p, data);
  printf("  %u addresses, %u communities\n", p.unique_keys, data->community_count);

  proto_counted_reset_peak();
  tx_index index;
  arnm_mono_timer timer;
  arnm_mono_timer_reset(&timer);
  index_build(&index, &p);
  uint64_t build_nanos = (uint64_t)arnm_mono_timer_nanos(timer);
  uint64_t adds = 0;
  for (uint32_t i = 0; i < index.address_count; ++i) {
    adds += set_cardinality(&index.addresses[i].balance) +
            set_cardinality(&index.addresses[i].signer) +
            set_cardinality(&index.addresses[i].other);
  }
  for (uint32_t t = 0; t < GRDT_TRANSACTION_COUNT; ++t) {
    adds += set_cardinality(&index.per_type[t]);
  }
  char memory[160];
  backend_memory(memory, sizeof(memory));
  char per_add[BENCH_STRING_BUFFER_SIZE];
  bench_per_step_string(per_add, sizeof(per_add), adds ? (double)build_nanos / (double)adds : 0.0);
  printf(
      "  build: %.2f ms, %llu adds, %s/add\n  memory: %s\n", (double)build_nanos / 1e6,
      (unsigned long long)adds, per_add, memory
  );

  // the middle half of the chain's time span, as transaction numbers
  query_context ctx = {&index, index.min_tx, index.max_tx, NULL, 0, 1};
  if (data->count) {
    int64_t first = data->records[0].confirmed_seconds;
    int64_t last = data->records[data->count - 1u].confirmed_seconds;
    int64_t from = first + (last - first) / 4;
    int64_t to = first + 3 * (last - first) / 4;
    uint32_t i = 0;
    while (i < data->count && data->records[i].confirmed_seconds < from) { ++i; }
    ctx.range_min = i < data->count ? data->records[i].tx_nr : index.max_tx;
    uint32_t j = data->count;
    while (j > 0 && data->records[j - 1u].confirmed_seconds > to) { --j; }
    ctx.range_max = j ? data->records[j - 1u].tx_nr : index.min_tx;
    printf(
        "  date range: tx %llu..%llu of %llu..%llu\n", (unsigned long long)ctx.range_min,
        (unsigned long long)ctx.range_max, (unsigned long long)index.min_tx,
        (unsigned long long)index.max_tx
    );
  }

  uint32_t sample_count = p.unique_keys < SAMPLE_KEYS ? p.unique_keys : SAMPLE_KEYS;
  uint32_t *sample =
      (uint32_t *)malloc((size_t)(sample_count ? sample_count : 1u) * sizeof(uint32_t));
  for (uint32_t i = 0; i < sample_count; ++i) {
    sample[i] = (uint32_t)((uint64_t)i * p.unique_keys / sample_count);
  }
  uint32_t *by_activity =
      (uint32_t *)malloc((size_t)(p.unique_keys ? p.unique_keys : 1u) * sizeof(uint32_t));
  for (uint32_t i = 0; i < p.unique_keys; ++i) { by_activity[i] = i; }
  g_sort_index = &index;
  qsort(by_activity, p.unique_keys, sizeof(uint32_t), compare_keys_by_balance);
  uint32_t top_count = p.unique_keys < TOP_KEYS ? p.unique_keys : TOP_KEYS;
  printf(
      "  busiest address: %llu balance changes\n",
      top_count ? (unsigned long long)set_cardinality(&index.addresses[by_activity[0]].balance)
                : 0ull
  );

  printf("\n  %-44s %11s %17s\n", "query", "total", "per query");
  ctx.keys = sample;
  ctx.key_count = sample_count;
  ctx.repeat = 1;
  run_query("sample: involved, newest 20", q_involved_newest, &ctx);
  run_query("sample: involved, count in date range", q_involved_count_range, &ctx);
  run_query("sample: balance+TRANSFER+range, count+page 2", q_balance_transfer_range_page, &ctx);
  run_query("sample: balance, count all", q_balance_count_all, &ctx);
  ctx.keys = by_activity;
  ctx.key_count = top_count;
  ctx.repeat = TOP_REPEAT;
  run_query("top20: involved, newest 20", q_involved_newest, &ctx);
  run_query("top20: involved, count in date range", q_involved_count_range, &ctx);
  run_query("top20: balance+TRANSFER+range, count+page 2", q_balance_transfer_range_page, &ctx);
  ctx.key_count = 1;
  ctx.repeat = WIDE_REPEAT;
  run_query("wide: TRANSFER in range, count+newest 20", q_transfer_range_newest, &ctx);
  run_query("wide: CREATION in range minus foreign coin", q_creation_without_foreign, &ctx);
  run_query("wide: TRANSFER AND DEFERRED in range, count", q_transfer_and_deferred_range, &ctx);

  free(sample);
  free(by_activity);
  index_free(&index);
  prepared_free(&p);
}

int main(int argc, char **argv) {
  arnm_mono_timer total;
  arnm_mono_timer_reset(&total);
  backend_init();
  printf("bitmap backend: %s\n", BACKEND_NAME);

  const char *path = bench_chain_data_path(argc, argv);
  bench_chain_data data;
  if (path && ARNM_SUCCESS == bench_chain_data_load(&data, path, bench_default_community_uuid)) {
    run_dataset(path, &data);
    bench_chain_data_free(&data);
  } else {
    printf("no chain file (argument or GRD_BENCH_CHAIN_DATA), chain dataset skipped\n");
  }
  if (ARNM_SUCCESS == bench_chain_data_synthetic(&data, SYNTHETIC_TX, SYNTHETIC_KEYS, 42u)) {
    run_dataset("synthetic 2M tx / 100k addresses", &data);
    bench_chain_data_free(&data);
  }

  char buffer[BENCH_STRING_BUFFER_SIZE];
  arnm_mono_timer_string(buffer, BENCH_STRING_BUFFER_SIZE, total);
  printf("\nall benchmarks: %s\n", buffer);
  return 0;
}
