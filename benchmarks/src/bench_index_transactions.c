#include "arnm/mono_timer.h"
#include "bench_chain_synth.h"
#include "bench_report.h"
#include "gradido_blockchain_core/blockchain/transactions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * What the transaction index costs on a real chain: filling it, and the questions a node asks of
 * it. Every row is one address, and addresses come in two shapes that answer very differently --
 * the few that sign or receive thousands of transactions, and the many that appear a handful of
 * times. Both are measured, because an average over them describes neither.
 *
 * Chain file: the first argument, else $GRD_BENCH_CHAIN_DATA, else
 * ../gradido_blockchain/build/tests/data/blk00000001.dat from the working directory.
 *
 * Usage: bench_index_transactions [chain file]
 */

/** Addresses per sample, and how far apart in the chain they are picked. */
#define SAMPLE_KEYS 64u
#define SAMPLE_STEP 97u
/** Rounds per row; the fastest counts, since everything slower is the machine doing something else.
 */
#define ROUNDS 7
#define PAGE 20u

static uint64_t g_sink;

typedef struct sample {
  uint8_t keys[SAMPLE_KEYS][SIGN_PUBLIC_KEY_SIZE];
  uint32_t count;
} sample;

/** What the walk over the chain fills while it reads. */
typedef struct fill_context {
  grdb_transactions *index;
  uint64_t nanos; /**< time inside grdb_transactions_add() */
  sample *signers;
  sample *receivers;
  uint32_t seen;
} fill_context;

/** The chain read once into the index, and two samples of addresses taken while reading. */
static arnm_result feed(const grdr_complete_transaction *tx, void *context) {
  fill_context *fill = (fill_context *)context;
  arnm_mono_timer timer;
  arnm_mono_timer_reset(&timer);
  const arnm_result result = grdb_transactions_add(fill->index, tx);
  fill->nanos += (uint64_t)arnm_mono_timer_nanos(timer);
  if (ARNM_SUCCESS != result) { return result; }

  if (fill->seen % SAMPLE_STEP == 0) {
    if (fill->signers->count < SAMPLE_KEYS && tx->signature_pairs_count) {
      memcpy(
          fill->signers->keys[fill->signers->count++], tx->signature_pairs[0].public_key,
          SIGN_PUBLIC_KEY_SIZE
      );
    }
    if (fill->receivers->count < SAMPLE_KEYS && tx->account_balances_count) {
      memcpy(
          fill->receivers->keys[fill->receivers->count++], tx->account_balances[0].pubkey,
          SIGN_PUBLIC_KEY_SIZE
      );
    }
  }
  ++fill->seen;
  return ARNM_SUCCESS;
}

/** One question asked of every address of @p keys, fastest of @ref ROUNDS runs. */
static double time_rows(
    const grdb_transactions *index,
    const sample *keys,
    const grdb_transactions_filter *shape,
    uint32_t skip,
    uint32_t size,
    bool newest_only
) {
  if (!keys->count) { return 0.0; }
  uint64_t best = UINT64_MAX;
  for (int round = 0; round < ROUNDS; ++round) {
    arnm_mono_timer timer;
    arnm_mono_timer_reset(&timer);
    for (uint32_t k = 0; k < keys->count; ++k) {
      grdb_transactions_filter filter = *shape;
      memcpy(filter.public_key, keys->keys[k], SIGN_PUBLIC_KEY_SIZE);
      if (newest_only) {
        uint64_t newest = 0;
        g_sink += grdb_transactions_newest(index, &filter, &newest) + newest;
        continue;
      }
      uint64_t page[PAGE];
      uint64_t count = 0;
      uint32_t written = 0;
      if (ARNM_SUCCESS ==
          grdb_transactions_listing(index, &filter, skip, size, true, page, &written, &count)) {
        g_sink += count + written + (written ? page[0] : 0u);
      }
    }
    const uint64_t nanos = (uint64_t)arnm_mono_timer_nanos(timer);
    if (nanos < best) { best = nanos; }
  }
  return (double)best / (double)keys->count;
}

static void report(const grdb_transactions *index, const char *title, const sample *keys) {
  bench_section(title);
  grdb_transactions_filter involved = {0};
  involved.role = GRDB_ADDRESS_ROLE_INVOLVED;
  grdb_transactions_filter balance = {0};
  balance.role = GRDB_ADDRESS_ROLE_BALANCE;
  grdb_transactions_filter transfers = involved;
  transfers.transaction_type = GRDT_TRANSACTION_TRANSFER;

  const struct {
    const char *name;
    const grdb_transactions_filter *filter;
    uint32_t skip;
    bool newest_only;
  } rows[] = {
      {"  involved, newest 1 (validation)", &involved, 0, true},
      {"  involved, count + newest 20", &involved, 0, false},
      {"  balance changing, count + newest 20", &balance, 0, false},
      {"  involved AND TRANSFER, count + page 2", &transfers, PAGE, false},
  };
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
    const double nanos =
        time_rows(index, keys, rows[i].filter, rows[i].skip, PAGE, rows[i].newest_only);
    char per_step[BENCH_STRING_BUFFER_SIZE];
    bench_per_step_string(per_step, BENCH_STRING_BUFFER_SIZE, nanos);
    printf("%-*s %12s/address\n", BENCH_NAME_WIDTH, rows[i].name, per_step);
  }
}

int main(int argc, char **argv) {
  const bench_chain_source source = bench_chain_source_of(argc, argv);

  static const uint8_t community_uuid[ARNM_UUID_BINARY_SIZE] = {0x3a, 0x3d, 0x7b, 0x4c, 0x1f, 0x9e,
                                                                0x4a, 0x61, 0x8d, 0x2b, 0x6c, 0x05,
                                                                0x9f, 0x77, 0xe3, 0x12};
  grdb_transactions index;
  grdb_transactions_options options = {0};
  options.expected_addresses = 20000;
  if (ARNM_SUCCESS != grdb_transactions_init(&index, &options, NULL)) {
    printf("no memory for the index\n");
    return 1;
  }

  sample signers = {{{0}}, 0};
  sample receivers = {{{0}}, 0};
  fill_context fill;
  memset(&fill, 0, sizeof(fill));
  fill.index = &index;
  fill.signers = &signers;
  fill.receivers = &receivers;
  uint32_t transactions = 0;
  arnm_mono_timer whole;
  arnm_mono_timer_reset(&whole);
  const arnm_result result =
      bench_chain_source_feed(&source, community_uuid, feed, &fill, &transactions);
  if (ARNM_SUCCESS != result || !transactions) {
    printf(
        "no chain at %s -- pass a file as the first argument, or none to generate one\n",
        source.path ? source.path : "(generated)"
    );
    grdb_transactions_release(&index);
    return 1;
  }

  bench_chain_source_print(&source);
  printf(
      "  %u transactions, %u addresses, fill %.2f ms (%.1f ns per transaction)\n", transactions,
      grdb_transactions_address_count(&index), (double)fill.nanos / 1e6,
      (double)fill.nanos / (double)transactions
  );
  printf(
      "  blocks: %.2f MiB lent, %.2f MiB on the free lists\n",
      (double)index.pool.lent_bytes / 1048576.0, (double)index.pool.cached_bytes / 1048576.0
  );

  report(&index, "addresses that sign (the busiest)", &signers);
  report(&index, "addresses whose balance moves (ordinary)", &receivers);
  bench_total(whole, (int)transactions, "transaction");

  grdb_transactions_release(&index);
  return (int)(g_sink & 0u);
}
