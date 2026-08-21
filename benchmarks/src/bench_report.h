#ifndef GRADIDO_BLOCKCHAIN_CORE_BENCH_REPORT_H
#define GRADIDO_BLOCKCHAIN_CORE_BENCH_REPORT_H

#include "hostmem/mono_timer.h"

#include <stdio.h>

/*
 * One report layout for every bench_* binary. They used to each carry their own bench_step and
 * drifted apart; sharing it here keeps the columns comparable when run_all.sh prints all three
 * in a row.
 *
 * Four line kinds, from one release run of bench_json:
 *
 *   time for prepare test data: 10.1590 us
 *
 *   complete transaction to json, compact
 *     minimal, no arrays                       18.7247 ms    374.5 ns/transaction
 *     typical transfer                         36.5328 ms    730.7 ns/transaction
 *     large, 4 KB body_bytes                  114.6053 ms      2.3 us/transaction
 *
 *   reference, for reading the rows above
 *     clone of the finished text into result    1.3061 ms     26.1 ns/copy
 *
 *   all benchmarks: 682.4179 ms, transactions per step: 50000
 *
 * The figures are one machine's and will not reproduce; the layout is the point. What the last
 * column names comes from bench_step()'s @p unit, which is why the clone row counts copies
 * while the rows above it count transactions.
 *
 * Both time columns pick their own unit, so the 374.5 ns step and the 2.3 us one above are
 * equally readable — a fixed ns column would drown the second in digits.
 */

#define BENCH_STRING_BUFFER_SIZE 32
/** Width of the name column; keep step names below it so the columns stay aligned. */
#define BENCH_NAME_WIDTH 40

/*
 * Picks a unit for the per step figure and keeps one decimal at every scale. Not
 * hostmem_duration_string: that takes whole nanoseconds, and a step costing 4.2 ns would arrive
 * there as 4 — the fraction is the interesting part at this end of the range.
 */
static inline void bench_per_step_string(char *buffer, size_t buffer_size, double nanos) {
  if (nanos < 1000.0) {
    snprintf(buffer, buffer_size, "%.1f ns", nanos);
  } else if (nanos < 1000000.0) {
    snprintf(buffer, buffer_size, "%.1f us", nanos / 1000.0);
  } else if (nanos < 1000000000.0) {
    snprintf(buffer, buffer_size, "%.1f ms", nanos / 1000000.0);
  } else {
    snprintf(buffer, buffer_size, "%.1f s", nanos / 1000000000.0);
  }
}

/** Header line, printed once before the first section. */
static inline void bench_prepared(hostmem_mono_timer time_used) {
  char buffer[BENCH_STRING_BUFFER_SIZE];
  hostmem_mono_timer_string(buffer, BENCH_STRING_BUFFER_SIZE, time_used);
  printf("time for prepare test data: %s\n", buffer);
}

/** Blank line, then a heading. Steps below it are named with two leading spaces. */
static inline void bench_section(const char *title) {
  printf("\n%s\n", title);
}

/**
 * Runs one step and prints its row.
 *
 * @param unit  what a single step processed, e.g. "element" or "derivation" — it names the
 *              per step figure and reappears in bench_total().
 */
static inline void bench_step(
    void (*func_ptr)(int), int step_count, const char *name, const char *unit
) {
  char total[BENCH_STRING_BUFFER_SIZE];
  char per_step[BENCH_STRING_BUFFER_SIZE];
  hostmem_mono_timer time_used;

  hostmem_mono_timer_reset(&time_used);
  func_ptr(step_count);
  hostmem_mono_timer_string(total, BENCH_STRING_BUFFER_SIZE, time_used);

  double nanos =
      step_count > 0 ? (double)hostmem_mono_timer_nanos(time_used) / (double)step_count : 0.0;
  bench_per_step_string(per_step, BENCH_STRING_BUFFER_SIZE, nanos);

  printf("%-*s %12s  %10s/%s\n", BENCH_NAME_WIDTH, name, total, per_step, unit);
}

/** Closing line: wall clock for the whole run and what one step was. */
static inline void bench_total(hostmem_mono_timer time_used, int step_count, const char *unit) {
  char buffer[BENCH_STRING_BUFFER_SIZE];
  hostmem_mono_timer_string(buffer, BENCH_STRING_BUFFER_SIZE, time_used);
  printf("\nall benchmarks: %s, %ss per step: %d\n", buffer, unit, step_count);
}

#endif // GRADIDO_BLOCKCHAIN_CORE_BENCH_REPORT_H
