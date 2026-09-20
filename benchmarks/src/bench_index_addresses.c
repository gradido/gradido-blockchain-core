#include "arnm/mono_timer.h"
#include "bench_chain_synth.h"
#include "bench_report.h"
#include "gradido_blockchain_core/index/addresses.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * What the address index costs on a real chain: filling it, and the two questions a validation
 * asks in front of almost every transaction -- what kind of address is this, as of that
 * transaction number, and when did its balance last move. Both are a map lookup and a step or
 * two, so the numbers here are mostly what hashing 32 bytes costs.
 *
 * Chain file: the first argument, else $GRD_BENCH_CHAIN_DATA, else
 * ../gradido_blockchain/build/tests/data/blk00000001.dat from the working directory.
 *
 * Usage: bench_index_addresses [chain file]
 */

#define SAMPLE_KEYS 64u
#define ROUNDS 7

static uint64_t g_sink;

typedef struct fill_context {
  grdx_addresses *index;
  uint64_t nanos; /**< time inside grdx_addresses_add() */
  uint8_t typed[SAMPLE_KEYS][SIGN_PUBLIC_KEY_SIZE];
  uint32_t typed_count; /**< addresses a transaction gave a type */
  uint8_t moved[SAMPLE_KEYS][SIGN_PUBLIC_KEY_SIZE];
  uint32_t moved_count; /**< addresses whose balance moved */
  uint64_t max_tx_nr;
} fill_context;

static arnm_result feed(const grdr_complete_transaction *tx, void *context) {
  fill_context *fill = (fill_context *)context;
  arnm_mono_timer timer;
  arnm_mono_timer_reset(&timer);
  const arnm_result result = grdx_addresses_add(fill->index, tx);
  fill->nanos += (uint64_t)arnm_mono_timer_nanos(timer);
  if (ARNM_SUCCESS != result) { return result; }
  fill->max_tx_nr = tx->tx_nr;

  if (fill->typed_count < SAMPLE_KEYS &&
      GRDT_TRANSACTION_REGISTER_ADDRESS == tx->transaction_type) {
    memcpy(
        fill->typed[fill->typed_count++], tx->register_address.user_public_key, SIGN_PUBLIC_KEY_SIZE
    );
  }
  if (fill->moved_count < SAMPLE_KEYS && tx->account_balances_count) {
    memcpy(fill->moved[fill->moved_count++], tx->account_balances[0].pubkey, SIGN_PUBLIC_KEY_SIZE);
  }
  return ARNM_SUCCESS;
}

/** One question asked of every address of a sample, fastest of @ref ROUNDS runs. */
static double time_rows(
    const grdx_addresses *index,
    const uint8_t keys[SAMPLE_KEYS][SIGN_PUBLIC_KEY_SIZE],
    uint32_t count,
    uint64_t at_tx_nr,
    int question
) {
  if (!count) { return 0.0; }
  uint64_t best = UINT64_MAX;
  for (int round = 0; round < ROUNDS; ++round) {
    arnm_mono_timer timer;
    arnm_mono_timer_reset(&timer);
    for (uint32_t k = 0; k < count; ++k) {
      switch (question) {
      case 0: {
        grdt_address type = GRDT_ADDRESS_NONE;
        g_sink += grdx_addresses_type_at(index, keys[k], at_tx_nr, &type) + (uint64_t)type;
        break;
      }
      case 1:
        g_sink += (uint64_t)grdx_addresses_type(index, keys[k]);
        break;
      default: {
        uint64_t last = 0;
        g_sink += grdx_addresses_last_balance(index, keys[k], &last) + last;
        break;
      }
      }
    }
    const uint64_t nanos = (uint64_t)arnm_mono_timer_nanos(timer);
    if (nanos < best) { best = nanos; }
  }
  return (double)best / (double)count;
}

static void report_row(const char *name, double nanos) {
  char per_step[BENCH_STRING_BUFFER_SIZE];
  bench_per_step_string(per_step, BENCH_STRING_BUFFER_SIZE, nanos);
  printf("%-*s %12s/address\n", BENCH_NAME_WIDTH, name, per_step);
}

int main(int argc, char **argv) {
  const bench_chain_source source = bench_chain_source_of(argc, argv);
  static const uint8_t community_uuid[ARNM_UUID_BINARY_SIZE] = {0x3a, 0x3d, 0x7b, 0x4c, 0x1f, 0x9e,
                                                                0x4a, 0x61, 0x8d, 0x2b, 0x6c, 0x05,
                                                                0x9f, 0x77, 0xe3, 0x12};

  grdx_addresses index;
  grdx_addresses_options options = {0};
  options.expected_addresses = 20000;
  if (ARNM_SUCCESS != grdx_addresses_init(&index, &options, NULL)) {
    printf("no memory for the index\n");
    return 1;
  }

  fill_context fill;
  memset(&fill, 0, sizeof(fill));
  fill.index = &index;
  arnm_mono_timer whole;
  arnm_mono_timer_reset(&whole);
  uint32_t transactions = 0;
  if (ARNM_SUCCESS !=
      bench_chain_source_feed(&source, community_uuid, feed, &fill, &transactions)) {
    printf(
        "no chain at %s -- pass a file as the first argument, or none to generate one\n",
        source.path ? source.path : "(generated)"
    );
    grdx_addresses_release(&index);
    return 1;
  }

  bench_chain_source_print(&source);
  printf(
      "  %u transactions, %u addresses, fill %.2f ms (%.1f ns per transaction)\n", transactions,
      grdx_addresses_size(&index), (double)fill.nanos / 1e6,
      (double)fill.nanos / (double)transactions
  );

  bench_section("addresses a registration typed");
  report_row(
      "  type as of the newest transaction",
      time_rows(&index, fill.typed, fill.typed_count, fill.max_tx_nr, 0)
  );
  report_row(
      "  type as of the middle of the chain",
      time_rows(&index, fill.typed, fill.typed_count, fill.max_tx_nr / 2u, 0)
  );
  report_row("  type, latest", time_rows(&index, fill.typed, fill.typed_count, 0, 1));

  bench_section("addresses whose balance moved");
  report_row("  last balance change", time_rows(&index, fill.moved, fill.moved_count, 0, 2));
  report_row(
      "  type as of the newest transaction",
      time_rows(&index, fill.moved, fill.moved_count, fill.max_tx_nr, 0)
  );

  bench_total(whole, (int)transactions, "transaction");
  grdx_addresses_release(&index);
  return (int)(g_sink & 0u);
}
