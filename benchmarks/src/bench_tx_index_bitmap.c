#include "arnm/mono_timer.h"
#include "bench_bitmap_backend.h"
#include "bench_chain_data.h"
#include "bench_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The transaction index over three set implementations, side by side.
 *
 * Every backend builds the same index from the same chain and answers the same ten questions,
 * and the table puts their numbers in one row each so a reader compares by looking rather than
 * by remembering. What each backend does with a question is its own business: CRoaring has no
 * ranged intersection and cuts the range out of the result, these sets apply it while reading.
 *
 * The answers themselves are not printed. They are compared: every row carries what it counted
 * and a fold of every value it read, and a run whose backends disagree prints which row and
 * stops -- timings of a wrong answer are worth nothing.
 *
 * Usage: bench_tx_index_bitmap [chain file]   (or GRD_BENCH_CHAIN_DATA)
 */

#define SYNTHETIC_TX 2000000u
#define SYNTHETIC_KEYS 100000u

static const bench_bitmap_backend *const g_backends[] = {
    &bench_bitmap_arnm, &bench_bitmap_croaring32, &bench_bitmap_croaring64
};
#define BACKEND_COUNT (sizeof(g_backends) / sizeof(g_backends[0]))

/** The value column, wide enough for "1234.5 ns" and "12.3 us". */
#define VALUE_WIDTH 11
/** The name column; the longest row name is 43 characters. */
#define NAME_WIDTH 45

/**
 * Whether every backend answered every row the same.
 *
 * Prints the first disagreement it finds -- the row, and what the two backends made of it --
 * and answers false, because a table of timings would then describe three different questions.
 */
static bool answers_agree(const bench_bitmap_report *reports, uint32_t count) {
  for (uint32_t row = 0; row < reports[0].row_count; ++row) {
    for (uint32_t i = 1; i < count; ++i) {
      if (reports[i].row_count != reports[0].row_count) {
        printf(
            "\n%s answered %u rows, %s answered %u\n", reports[i].backend, reports[i].row_count,
            reports[0].backend, reports[0].row_count
        );
        return false;
      }
      const bench_bitmap_row *a = &reports[0].rows[row];
      const bench_bitmap_row *b = &reports[i].rows[row];
      if (a->counts == b->counts && a->hash == b->hash) { continue; }
      printf("\nthe backends disagree on \"%s\"\n", a->name);
      printf(
          "  %-32s counted %llu, values %016llx\n", reports[0].backend,
          (unsigned long long)a->counts, (unsigned long long)a->hash
      );
      printf(
          "  %-32s counted %llu, values %016llx\n", reports[i].backend,
          (unsigned long long)b->counts, (unsigned long long)b->hash
      );
      return false;
    }
  }
  return true;
}

static void print_value(double nanos) {
  char text[BENCH_STRING_BUFFER_SIZE];
  bench_per_step_string(text, sizeof(text), nanos);
  printf(" %*s", VALUE_WIDTH, text);
}

/** The table for one chain: a line per row, a column per backend. */
static void print_table(const bench_bitmap_report *reports, uint32_t count) {
  printf("\n  %-*s", NAME_WIDTH, "");
  for (uint32_t i = 0; i < count; ++i) { printf(" %*s", VALUE_WIDTH, g_backends[i]->label); }
  printf("\n");

  printf("  %-*s", NAME_WIDTH, "build, per value added");
  for (uint32_t i = 0; i < count; ++i) { print_value(reports[i].nanos_per_add); }
  printf("\n");

  printf("  %-*s", NAME_WIDTH, "memory of the sets, MiB");
  for (uint32_t i = 0; i < count; ++i) { printf(" %*.1f", VALUE_WIDTH, reports[i].live_mib); }
  printf("\n");

  printf("  %-*s", NAME_WIDTH, "memory taken from the host, MiB");
  for (uint32_t i = 0; i < count; ++i) { printf(" %*.1f", VALUE_WIDTH, reports[i].reserved_mib); }
  printf("\n\n");

  for (uint32_t row = 0; row < reports[0].row_count; ++row) {
    printf("  %-*s", NAME_WIDTH, reports[0].rows[row].name);
    for (uint32_t i = 0; i < count; ++i) { print_value(reports[i].rows[row].nanos); }
    printf("\n");
  }
}

/** Builds the index three ways over @p data and prints what each of them cost. */
static bool run_dataset(const char *title, const bench_chain_data *data) {
  printf("\n=== %s: %u transactions\n", title, data->count);
  bench_bitmap_report reports[BACKEND_COUNT];
  for (uint32_t i = 0; i < BACKEND_COUNT; ++i) { g_backends[i]->run(data, &reports[i]); }

  printf(
      "  %u addresses, %u communities, transactions %llu..%llu, the middle half of the days is "
      "%llu..%llu\n  busiest address: %llu balance changes, %llu values added to the sets\n",
      reports[0].addresses, data->community_count, (unsigned long long)reports[0].min_tx,
      (unsigned long long)reports[0].max_tx, (unsigned long long)reports[0].range_min,
      (unsigned long long)reports[0].range_max, (unsigned long long)reports[0].busiest,
      (unsigned long long)reports[0].adds
  );

  if (!answers_agree(reports, BACKEND_COUNT)) { return false; }
  print_table(reports, BACKEND_COUNT);
  return true;
}

int main(int argc, char **argv) {
  arnm_mono_timer total;
  arnm_mono_timer_reset(&total);

  printf("transaction index, one chain, three set implementations\n");
  for (uint32_t i = 0; i < BACKEND_COUNT; ++i) {
    printf("  %-6s %s\n", g_backends[i]->label, g_backends[i]->name);
  }

  bool agreed = true;
  const char *path = bench_chain_data_path(argc, argv);
  bench_chain_data data;
  if (path && ARNM_SUCCESS == bench_chain_data_load(&data, path, bench_default_community_uuid)) {
    agreed = run_dataset(path, &data);
    bench_chain_data_free(&data);
  } else {
    printf("\nno chain file (argument or GRD_BENCH_CHAIN_DATA), chain dataset skipped\n");
  }
  if (agreed &&
      ARNM_SUCCESS == bench_chain_data_synthetic(&data, SYNTHETIC_TX, SYNTHETIC_KEYS, 42u)) {
    agreed = run_dataset("synthetic 2M tx / 100k addresses", &data);
    bench_chain_data_free(&data);
  }
  if (!agreed) {
    printf("\nstopped: the backends do not answer the same, so their timings compare nothing\n");
    return 1;
  }

  char buffer[BENCH_STRING_BUFFER_SIZE];
  arnm_mono_timer_string(buffer, BENCH_STRING_BUFFER_SIZE, total);
  printf("\nall benchmarks: %s\n", buffer);
  return 0;
}
