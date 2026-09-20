#ifndef GRADIDO_BLOCKCHAIN_CORE_BENCH_BITMAP_BACKEND_H
#define GRADIDO_BLOCKCHAIN_CORE_BENCH_BITMAP_BACKEND_H

#include "bench_chain_data.h"

#include <stdint.h>

/**
 * One transaction index, built three ways, measured in one run.
 *
 * Every backend answers the same questions over the same chain, and each is used the way it is
 * best at them rather than the way the others are. What a backend hands back is numbers, not
 * output: the driver prints them side by side, because a comparison a reader has to remember
 * between two program runs is not a comparison.
 *
 * A backend is one translation unit: it defines BENCH_BITMAP_* and includes
 * `bench_bitmap_body.h`, which is written once and compiled once per backend. That is why
 * everything in the body is static and why each unit exposes exactly one symbol -- the
 * descriptor below.
 */

/** Query rows one run can report. */
#define BENCH_BITMAP_ROW_MAX 16u

/** One query row: what it cost, and what it answered. */
typedef struct bench_bitmap_row {
  const char *name; /**< What the row asks; the same text in every backend. */
  double nanos;     /**< Nanoseconds per query. */
  uint64_t counts;  /**< Everything the row counted -- half of what makes the answers equal. */
  uint64_t hash;    /**< Every value it read, folded -- the other half. */
} bench_bitmap_row;

/** What one backend has to say about one chain. */
typedef struct bench_bitmap_report {
  const char *backend;  /**< Full name, for the legend. */
  double build_ms;      /**< Time to build every set of the index. */
  double nanos_per_add; /**< That time per value added. */
  uint64_t adds;        /**< Values added while building. */
  double live_mib;      /**< What the sets hold when the build is done. */
  double reserved_mib;  /**< What was taken from the host to hold it. */
  uint32_t addresses;   /**< Addresses the chain named. */
  uint64_t busiest;     /**< Balance changes of the busiest address. */
  uint64_t min_tx;      /**< First transaction number of the chain. */
  uint64_t max_tx;      /**< Last one. */
  uint64_t range_min;   /**< The date range the rows use, as numbers. */
  uint64_t range_max;
  uint32_t row_count;
  bench_bitmap_row rows[BENCH_BITMAP_ROW_MAX];
} bench_bitmap_report;

/** Builds the index over @p data and fills @p report; prints nothing. */
typedef void (*bench_bitmap_run_fn)(const bench_chain_data *data, bench_bitmap_report *report);

/** A backend, as the driver sees it. */
typedef struct bench_bitmap_backend {
  const char *name;  /**< Full name for the legend. */
  const char *label; /**< Short name for the table's column head. */
  bench_bitmap_run_fn run;
} bench_bitmap_backend;

extern const bench_bitmap_backend bench_bitmap_arnm;
extern const bench_bitmap_backend bench_bitmap_croaring32;
extern const bench_bitmap_backend bench_bitmap_croaring64;

#endif // GRADIDO_BLOCKCHAIN_CORE_BENCH_BITMAP_BACKEND_H
