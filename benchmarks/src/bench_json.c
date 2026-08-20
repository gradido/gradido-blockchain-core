#include "bench_report.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/mapping/json_from_runtime.h"
#include "hostmem/memory.h"
#include "hostmem/mono_timer.h"
#include "hostmem/multi_arena.h"

#include <stdio.h>
#include <string.h>

/*
 * What one transaction costs on its way out as JSON, and what the two arenas buy.
 *
 * Three shapes are measured, because the cost follows the bytes rather than the transaction:
 * a bare creation carries nothing but its own fields, a typical transfer adds the arrays, and
 * a transaction with body_bytes carries a single hex string longer than everything else put
 * together. Each is rendered compact and pretty.
 *
 * The chain section is the reason the mapping takes two allocators at all. The rows there run
 * the same transaction through chains in three states -- reset between calls, released and
 * rebuilt between calls, and borrowed from storage the caller owns -- so the difference between
 * them is what the arena reuse is worth, measured rather than asserted.
 *
 * Run this in a release build:
 *
 *   zig build -Dtarget=x86_64-linux-gnu -Dbenchmarks=true -Doptimize=ReleaseFast
 *
 * `zig build` defaults to Debug, which instruments every C source with UBSan; the figures come
 * out around twenty times what a release build measures, and the ratios between the rows shift
 * as well. A Debug run says nothing about this code.
 */

#define STEP_COUNT 50000
#define LARGE_STEP_COUNT 5000

// ****************** the transactions under test *******************************************

#define TYPICAL_BALANCES 2
#define TYPICAL_SIGNATURES 2
#define TYPICAL_MEMO_SIZE 64

#define LARGE_BALANCES 8
#define LARGE_SIGNATURES 3
#define LARGE_MEMO_SIZE 256
#define LARGE_BODY_SIZE 4096

/** @brief Compact text size of the typical transfer, as the size rows below report it. */
#define TYPICAL_TEXT_SIZE 1731

static const uint8_t community_uuid[HOSTMEM_UUID_BINARY_SIZE] = {0x01, 0x9e, 0x2c, 0x31, 0xa3, 0x03,
                                                                 0x75, 0xc0, 0x94, 0x1e, 0xf3, 0x5c,
                                                                 0x59, 0xe4, 0xf9, 0x78};

static grdw_account_balance typical_balances[TYPICAL_BALANCES];
static grdw_signature_pair typical_signatures[TYPICAL_SIGNATURES];
static uint8_t typical_memo_bytes[TYPICAL_MEMO_SIZE];
static grdw_encrypted_memo typical_memos[1];

static grdw_account_balance large_balances[LARGE_BALANCES];
static grdw_signature_pair large_signatures[LARGE_SIGNATURES];
static uint8_t large_memo_bytes[LARGE_MEMO_SIZE];
static grdw_encrypted_memo large_memos[2];
static uint8_t large_body_bytes[LARGE_BODY_SIZE];

static grdr_complete_transaction tx_minimal;
static grdr_complete_transaction tx_typical;
static grdr_complete_transaction tx_large;

//! Fill with a ramp: cheap, deterministic, and every byte differs from its neighbour.
static void ramp(uint8_t *data, size_t size, uint8_t first) {
  for (size_t i = 0; i < size; ++i) { data[i] = (uint8_t)(first + i); }
}

static void prepare_transfer(grdr_complete_transaction *tx) {
  grdr_complete_transaction_init(tx);
  tx->tx_nr = 121;
  tx->created_at.seconds = 1700000000;
  tx->created_at.nanos = 2912;
  tx->confirmed_at.seconds = 1700000060;
  memcpy(tx->tx_community_uuid, community_uuid, HOSTMEM_UUID_BINARY_SIZE);
  tx->ledger_anchor.type = GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_ID;
  tx->ledger_anchor.id = 4711;
  tx->transaction_type = GRDT_TRANSACTION_TRANSFER;
  tx->balance_derivation_type = GRDT_BALANCE_DERIVATION_NODE;
  ramp(tx->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x10);
  ramp(tx->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x40);
  tx->transfer.amount = 12345;
  memcpy(tx->transfer.coin_community_uuid, community_uuid, HOSTMEM_UUID_BINARY_SIZE);
  ramp(tx->tx_running_hash, GENERIC_HASH_SIZE, 0x80);
}

static void prepare_balances(grdw_account_balance *balances, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    ramp(balances[i].pubkey, SIGN_PUBLIC_KEY_SIZE, (uint8_t)(0x40 + i));
    balances[i].balance = 987650000;
    memcpy(balances[i].community_uuid, community_uuid, HOSTMEM_UUID_BINARY_SIZE);
  }
}

static void prepare_signatures(grdw_signature_pair *signatures, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    ramp(signatures[i].public_key, SIGN_PUBLIC_KEY_SIZE, (uint8_t)(0x10 + i));
    ramp(signatures[i].signature, SIGN_SIGNATURE_SIZE, (uint8_t)(0x01 + i));
  }
}

static void prepare_transactions() {
  // a creation with no arrays at all: the floor of what the mapping has to write
  grdr_complete_transaction_init(&tx_minimal);
  tx_minimal.tx_nr = 1;
  tx_minimal.created_at.seconds = 1700000000;
  tx_minimal.transaction_type = GRDT_TRANSACTION_CREATION;
  ramp(tx_minimal.transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x40);
  tx_minimal.transfer.amount = 10000000;
  tx_minimal.target_date = 1700000000;

  prepare_transfer(&tx_typical);
  prepare_balances(typical_balances, TYPICAL_BALANCES);
  prepare_signatures(typical_signatures, TYPICAL_SIGNATURES);
  ramp(typical_memo_bytes, TYPICAL_MEMO_SIZE, 0x01);
  typical_memos[0].type = GRDT_MEMO_KEY_SHARED_SECRET;
  typical_memos[0].memo.data = typical_memo_bytes;
  typical_memos[0].memo.size = TYPICAL_MEMO_SIZE;
  tx_typical.account_balances = typical_balances;
  tx_typical.account_balances_count = TYPICAL_BALANCES;
  tx_typical.signature_pairs = typical_signatures;
  tx_typical.signature_pairs_count = TYPICAL_SIGNATURES;
  tx_typical.encrypted_memos = typical_memos;
  tx_typical.encrypted_memos_count = 1;

  prepare_transfer(&tx_large);
  prepare_balances(large_balances, LARGE_BALANCES);
  prepare_signatures(large_signatures, LARGE_SIGNATURES);
  ramp(large_memo_bytes, LARGE_MEMO_SIZE, 0x01);
  for (int i = 0; i < 2; ++i) {
    large_memos[i].type = i ? GRDT_MEMO_KEY_PLAIN : GRDT_MEMO_KEY_SHARED_SECRET;
    large_memos[i].memo.data = large_memo_bytes;
    large_memos[i].memo.size = LARGE_MEMO_SIZE;
  }
  ramp(large_body_bytes, LARGE_BODY_SIZE, 0x07);
  tx_large.account_balances = large_balances;
  tx_large.account_balances_count = LARGE_BALANCES;
  tx_large.signature_pairs = large_signatures;
  tx_large.signature_pairs_count = LARGE_SIGNATURES;
  tx_large.encrypted_memos = large_memos;
  tx_large.encrypted_memos_count = 2;
  tx_large.body_bytes.data = large_body_bytes;
  tx_large.body_bytes.size = LARGE_BODY_SIZE;
}

// ****************** the chains ************************************************************

static hostmem_multi_arena work;
static hostmem_multi_arena result;

/*
 * Storage for the borrowed row. Sized from what the large transaction was measured to need,
 * with room to spare: a borrowed chain cannot open another arena when it runs out, it fails,
 * and a benchmark that silently measured a failing call would be worse than no row at all.
 */
_Alignas(8) static uint8_t borrowed_work[256 * 1024];
_Alignas(8) static uint8_t borrowed_result[64 * 1024];
_Alignas(8) static uint8_t borrowed_bookkeeping[4096];

/**
 * @brief Render @p tx @p step_count times, resetting both chains after each pass.
 *
 * The reset is inside the loop on purpose: it is what a caller does between transactions, and
 * leaving it out would measure a chain that grows without bound rather than the cycle the
 * mapping is built for. It is O(arena count) and does not reach the host.
 *
 * A failed render stops the run rather than being counted -- a row averaged over calls that
 * did nothing would read as a fast one.
 */
static void render_repeatedly(
    const grdr_complete_transaction *tx, grdm_json_format format, int step_count
) {
  hostmem_memory_block json = {0};
  for (int i = 0; i < step_count; ++i) {
    if (HOSTMEM_SUCCESS != grdm_complete_transaction_to_json(&json, tx, format, &work, &result)) {
      printf("  render failed, benchmark aborted\n");
      return;
    }
    hostmem_multi_arena_reset(&work);
    hostmem_multi_arena_reset(&result);
  }
}

static void bench_minimal_compact(int n) {
  render_repeatedly(&tx_minimal, GRDM_JSON_COMPACT, n);
}
static void bench_minimal_pretty(int n) {
  render_repeatedly(&tx_minimal, GRDM_JSON_PRETTY, n);
}
static void bench_typical_compact(int n) {
  render_repeatedly(&tx_typical, GRDM_JSON_COMPACT, n);
}
static void bench_typical_pretty(int n) {
  render_repeatedly(&tx_typical, GRDM_JSON_PRETTY, n);
}
static void bench_large_compact(int n) {
  render_repeatedly(&tx_large, GRDM_JSON_COMPACT, n);
}
static void bench_large_pretty(int n) {
  render_repeatedly(&tx_large, GRDM_JSON_PRETTY, n);
}

/**
 * @brief The same transaction through chains that are torn down and rebuilt every pass.
 *
 * Every render then has to open its arenas from the host, which is the one thing the reset
 * cycle above avoids. The gap between this row and "reset between calls" is what the arenas
 * are worth once they are there.
 */
static void bench_cold_chains(int step_count) {
  hostmem_memory_block json = {0};
  for (int i = 0; i < step_count; ++i) {
    hostmem_multi_arena cold_work = {0};
    hostmem_multi_arena cold_result = {0};
    if (HOSTMEM_SUCCESS != hostmem_multi_arena_init(&cold_work, 1 << 16, 0, NULL) ||
        HOSTMEM_SUCCESS != hostmem_multi_arena_init(&cold_result, 1 << 16, 0, NULL) ||
        HOSTMEM_SUCCESS != grdm_complete_transaction_to_json(
                               &json, &tx_typical, GRDM_JSON_COMPACT, &cold_work, &cold_result
                           )) {
      printf("  render failed, benchmark aborted\n");
      return;
    }
    hostmem_multi_arena_release(&cold_work);
    hostmem_multi_arena_release(&cold_result);
  }
}

/**
 * @brief The same transaction through chains made of storage the caller owns.
 *
 * Nothing here reaches the host, not even the descriptor vectors. This is the row a host with
 * a fixed memory budget is looking at.
 */
static void bench_borrowed_chains(int step_count) {
  hostmem bookkeeping = {0};
  hostmem_multi_arena borrowed_work_chain = {0};
  hostmem_multi_arena borrowed_result_chain = {0};
  hostmem_memory_block json = {0};

  if (HOSTMEM_SUCCESS != hostmem_init_arena_borrow(
                             &bookkeeping, borrowed_bookkeeping, sizeof(borrowed_bookkeeping)
                         ) ||
      HOSTMEM_SUCCESS !=
          hostmem_multi_arena_init(&borrowed_work_chain, sizeof(borrowed_work), 0, &bookkeeping) ||
      HOSTMEM_SUCCESS != hostmem_multi_arena_init(
                             &borrowed_result_chain, sizeof(borrowed_result), 0, &bookkeeping
                         ) ||
      HOSTMEM_SUCCESS !=
          hostmem_multi_arena_borrow(&borrowed_work_chain, borrowed_work, sizeof(borrowed_work)) ||
      HOSTMEM_SUCCESS != hostmem_multi_arena_borrow(
                             &borrowed_result_chain, borrowed_result, sizeof(borrowed_result)
                         )) {
    printf("  borrowed setup failed, benchmark aborted\n");
    return;
  }

  for (int i = 0; i < step_count; ++i) {
    if (HOSTMEM_SUCCESS !=
        grdm_complete_transaction_to_json(
            &json, &tx_typical, GRDM_JSON_COMPACT, &borrowed_work_chain, &borrowed_result_chain
        )) {
      printf("  render failed, benchmark aborted\n");
      return;
    }
    hostmem_multi_arena_reset(&borrowed_work_chain);
    hostmem_multi_arena_reset(&borrowed_result_chain);
  }

  hostmem_multi_arena_release(&borrowed_work_chain);
  hostmem_multi_arena_release(&borrowed_result_chain);
  hostmem_release(&bookkeeping);
}

/**
 * @brief What the finished text costs to place in the result chain: one alloc and one memcpy.
 *
 * The mapping renders through the work chain and copies the result across, rather than letting
 * yyjson write into the result chain directly. That copy is what this row prices. It is the
 * whole difference between the two designs on the time axis; the memory axis is in the module
 * comment of json_from_runtime.c, where the copy earns a result chain holding the text and
 * nothing else.
 */
static void bench_clone_reference(int step_count) {
  static uint8_t text[TYPICAL_TEXT_SIZE];
  uint8_t *copy = NULL;
  for (int i = 0; i < step_count; ++i) {
    if (HOSTMEM_SUCCESS != hostmem_multi_arena_clone(&copy, text, TYPICAL_TEXT_SIZE, &result)) {
      printf("  clone failed, benchmark aborted\n");
      return;
    }
    hostmem_multi_arena_reset(&result);
  }
}

// ****************** what each shape comes to *********************************************

/** @brief One render, to report the text size a row's per step figure belongs to. */
static uint32_t measure_size(const grdr_complete_transaction *tx, grdm_json_format format) {
  hostmem_memory_block json = {0};
  if (HOSTMEM_SUCCESS != grdm_complete_transaction_to_json(&json, tx, format, &work, &result)) {
    return 0;
  }
  uint32_t size = json.size;
  hostmem_multi_arena_reset(&work);
  hostmem_multi_arena_reset(&result);
  return size;
}

static void report_sizes() {
  printf("\ntext size per shape (compact / pretty, bytes)\n");
  printf(
      "%-*s %12u  %10u\n", BENCH_NAME_WIDTH, "  minimal, no arrays",
      measure_size(&tx_minimal, GRDM_JSON_COMPACT), measure_size(&tx_minimal, GRDM_JSON_PRETTY)
  );
  printf(
      "%-*s %12u  %10u\n", BENCH_NAME_WIDTH, "  typical transfer",
      measure_size(&tx_typical, GRDM_JSON_COMPACT), measure_size(&tx_typical, GRDM_JSON_PRETTY)
  );
  printf(
      "%-*s %12u  %10u\n", BENCH_NAME_WIDTH, "  large, 4 KB body_bytes",
      measure_size(&tx_large, GRDM_JSON_COMPACT), measure_size(&tx_large, GRDM_JSON_PRETTY)
  );
}

int main(void) {
  hostmem_mono_timer_init();
  hostmem_mono_timer time_used;

  hostmem_mono_timer_reset(&time_used);
  prepare_transactions();
  // 64 KiB arenas: the typical transaction fits one stretch, the large one walks the chain,
  // which is the ordinary case rather than a tuned one
  if (HOSTMEM_SUCCESS != hostmem_multi_arena_init(&work, 1 << 16, 0, NULL) ||
      HOSTMEM_SUCCESS != hostmem_multi_arena_init(&result, 1 << 16, 0, NULL)) {
    printf("chain setup failed\n");
    return 1;
  }
  bench_prepared(time_used);
  report_sizes();

  bench_section("complete transaction to json, compact");
  bench_step(bench_minimal_compact, STEP_COUNT, "  minimal, no arrays", "transaction");
  bench_step(bench_typical_compact, STEP_COUNT, "  typical transfer", "transaction");
  bench_step(bench_large_compact, LARGE_STEP_COUNT, "  large, 4 KB body_bytes", "transaction");

  bench_section("complete transaction to json, pretty");
  bench_step(bench_minimal_pretty, STEP_COUNT, "  minimal, no arrays", "transaction");
  bench_step(bench_typical_pretty, STEP_COUNT, "  typical transfer", "transaction");
  bench_step(bench_large_pretty, LARGE_STEP_COUNT, "  large, 4 KB body_bytes", "transaction");

  bench_section("chain state, typical transfer compact");
  bench_step(bench_typical_compact, STEP_COUNT, "  reset between calls", "transaction");
  bench_step(
      bench_borrowed_chains, STEP_COUNT, "  borrowed, never reaches the host", "transaction"
  );
  bench_step(bench_cold_chains, STEP_COUNT, "  rebuilt between calls", "transaction");

  bench_section("reference, for reading the rows above");
  bench_step(bench_clone_reference, STEP_COUNT, "  clone of the finished text into result", "copy");

  hostmem_multi_arena_release(&work);
  hostmem_multi_arena_release(&result);

  bench_total(time_used, STEP_COUNT, "transaction");
  return 0;
}
