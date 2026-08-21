#include "bench_report.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"
#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "gradido_blockchain_core/mapping/json_from_runtime.h"
#include "gradido_blockchain_core/mapping/json_from_wire.h"
#include "gradido_blockchain_core/mapping/wire_from_json.h"
#include "hostmem/memory.h"
#include "hostmem/mono_timer.h"
#include "hostmem/multi_arena.h"

#include <inttypes.h>
#include <stdbool.h>
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

/*
 * One step count for every row. The large rows used to run a tenth of the others because they
 * cost about three times as much per transaction, but two rows at a different step count make
 * the table misread at a glance -- the total time column looked like the large shape was the
 * cheap one -- and it left the closing "per step" line true of only sixteen of the eighteen
 * rows. Paying the extra wall clock is the cheaper of the two.
 */
#define STEP_COUNT 50000

// ****************** the transactions under test *******************************************

#define TYPICAL_BALANCES 2
#define TYPICAL_SIGNATURES 2
#define TYPICAL_MEMO_SIZE 64

#define LARGE_BALANCES 8
#define LARGE_SIGNATURES 3
#define LARGE_MEMO_SIZE 256
#define LARGE_BODY_SIZE 4096

/**
 * @brief Bytes of scratch the decoding rows hand pbtools, in a caller-owned static buffer.
 *
 * The encoded body those rows decode is under 512 bytes (see encoded_body below), so this
 * leaves pbtools more than an order of magnitude of headroom over the largest workspace that
 * body can ask for. Too small a value would make the decode fail and abort the row instead of
 * measuring it, so it is not tuned down to the minimum.
 */
#define PB_WORKSPACE_SIZE 8192u

/** @brief Compact text size of the typical transfer, as the size rows below report it. */
#define TYPICAL_TEXT_SIZE 1731

/*
 * Whether anything in this run failed. Every abort path sets it and main() returns it as the
 * process exit status, so a run that produced a partial table cannot be mistaken for a complete
 * one by whatever invoked the binary. The rows still print what they managed -- a row silently
 * missing from the table is easy to read past, and the printed abort line says which one broke.
 */
static int bench_failure;

/**
 * @brief Name a failure, print it under the row it belongs to, and fail the run.
 *
 * @param what  what could not be done, e.g. "render" -- it opens the printed line.
 *
 * Every failure path in this file goes through here rather than printing on its own, so none
 * of them can report the trouble and still leave the exit status at 0. Callers return early
 * afterwards; this function only records, it does not unwind anything.
 */
static void bench_fail(const char *what) {
  printf("  %s failed, benchmark aborted\n", what);
  bench_failure = 1;
}

/**
 * @brief Print a row that was not run, where its figures would have been.
 *
 * @param name why-less row label, in the same column as every other row.
 * @param why  what was missing, printed instead of the two time columns.
 *
 * A row left out with nothing printed would read as a row that does not exist, and the next
 * reader would go looking for why the table is one line short. The failure itself is already
 * on its way to the exit status through bench_fail(); this only accounts for the gap.
 */
static void bench_skipped(const char *name, const char *why) {
  printf("%-*s %s\n", BENCH_NAME_WIDTH, name, why);
}

/**
 * @brief Whether prepare_transactions() got the body the decoding rows are measured on.
 *
 * False leaves wire_confirmed.transaction.body_bytes empty, and that is not a fixture the
 * decoding rows can run against: each would abort on its first pass and report a per step
 * figure divided over no work, which is a row that looks fast rather than a row that is
 * missing. They are skipped instead, and so is the size line that decodes the same body.
 * The rows that read the body as hex still run -- an empty byte string renders, and what they
 * then measure is a transaction without a body, which the failed line above already says.
 */
static bool have_encoded_body;

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

static grdw_transaction_body wire_body;
static grdw_confirmed_transaction wire_confirmed;

//! The rendered text the reading rows below parse; filled once, in prepare_transactions().
static char wire_body_json[4096];
static size_t wire_body_json_size;
static char wire_confirmed_json[4096];
static size_t wire_confirmed_json_size;

static grdr_complete_transaction tx_minimal;
static grdr_complete_transaction tx_typical;
static grdr_complete_transaction tx_large;

/**
 * @brief Fill @p data with @p size consecutive byte values, beginning at @p first.
 *
 * @param data   destination buffer; must be writable for at least @p size bytes. Only read as
 *               a destination -- its previous contents are overwritten, never inspected.
 * @param size   number of bytes to write. 0 writes nothing and dereferences nothing, so a null
 *               @p data is only valid together with a zero @p size.
 * @param first  the value written to `data[0]`.
 *
 * `data[i]` is `(uint8_t)(first + i)`. The addition happens in `int` after the usual integer
 * promotions and is narrowed by the explicit cast, so the sequence wraps modulo 256 every 256
 * bytes with defined behaviour and no overflow at any @p size, including sizes past `SIZE_MAX
 * - first` worth of steps -- the wrap is the intent, not an edge case.
 *
 * The result depends on nothing but the arguments: no clock, no allocation, no global state.
 * That is what makes the fixtures below identical on every run, so figures from two runs are
 * comparable. Neighbouring bytes always differ, which keeps the hex writer off any run-length
 * or all-equal shortcut. One store per byte; the function returns nothing and cannot fail.
 *
 * Called from prepare_transactions() only, never from inside a measured loop.
 */
static void ramp(uint8_t *data, size_t size, uint8_t first) {
  for (size_t i = 0; i < size; ++i) { data[i] = (uint8_t)(first + i); }
}

/**
 * @brief Fill @p tx with the transfer the "typical" and "large" rows are measured on.
 *
 * @param tx  a writable transaction, not null. It is passed through
 *            grdr_complete_transaction_init() first, so any previous contents are discarded.
 *            The array members (balances, signatures, memos, body_bytes) are left empty here;
 *            the caller attaches them, which is what separates the typical shape from the
 *            large one.
 *
 * Every field is a literal, so the rendered text is byte-for-byte the same on every run.
 * `transfer.amount` is a grdd_unit: fixed point scaled by 10^4, so 12345 is 1.2345 GDD, not
 * 12345 GDD. The value is chosen to render four significant digits either side of the decimal
 * point rather than for its size. Returns nothing and cannot fail; grdr_complete_transaction
 * holds no owned memory at this point, so there is nothing to allocate and nothing to release.
 */
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

/**
 * @brief Fill the first @p count entries of @p balances with distinct, fixed balances.
 *
 * @param balances  array of at least @p count writable entries; may be null only when @p count
 *                  is 0, which writes nothing.
 * @param count     entries to fill. The callers pass TYPICAL_BALANCES and LARGE_BALANCES, both
 *                  well under 256, so the `(uint8_t)(0x40 + i)` ramp seed stays distinct per
 *                  entry; beyond 256 entries the seeds would repeat and entries would share
 *                  their pubkey bytes -- which would still render, just with less distinct
 *                  input than intended.
 *
 * `balance` is a grdd_unit, fixed point scaled by 10^4: 987650000 is 98765.0000 GDD. It is the
 * same in every entry on purpose -- what scales the row is the entry count, not the digits.
 * Deterministic, allocates nothing, returns nothing, cannot fail.
 */
static void prepare_balances(grdw_account_balance *balances, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    ramp(balances[i].pubkey, SIGN_PUBLIC_KEY_SIZE, (uint8_t)(0x40 + i));
    balances[i].balance = 987650000;
    memcpy(balances[i].community_uuid, community_uuid, HOSTMEM_UUID_BINARY_SIZE);
  }
}

/**
 * @brief Fill the first @p count entries of @p signatures with distinct key/signature bytes.
 *
 * @param signatures  array of at least @p count writable entries; may be null only when
 *                    @p count is 0, which writes nothing.
 * @param count       entries to fill. As in prepare_balances(), the per-entry ramp seeds are
 *                    narrowed to uint8_t and so stay distinct only for the first 256 entries;
 *                    the callers pass TYPICAL_SIGNATURES and LARGE_SIGNATURES.
 *
 * The bytes are not a valid signature over anything -- the mapping hex-encodes them without
 * verifying, so what matters is their length and that they differ from each other.
 * Deterministic, allocates nothing, returns nothing, cannot fail.
 */
static void prepare_signatures(grdw_signature_pair *signatures, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    ramp(signatures[i].public_key, SIGN_PUBLIC_KEY_SIZE, (uint8_t)(0x10 + i));
    ramp(signatures[i].signature, SIGN_SIGNATURE_SIZE, (uint8_t)(0x01 + i));
  }
}

/**
 * @brief Build every fixture the rows below run on, into the file-scope statics.
 *
 * Fills tx_minimal, tx_typical, tx_large, wire_body and wire_confirmed, and points their array
 * members at the static buffers above -- nothing is heap allocated and nothing needs releasing.
 * The transactions borrow those buffers rather than copying them, so the buffers must outlive
 * every render, which file scope guarantees.
 *
 * All amounts are grdd_unit fixed point scaled by 10^4 (10000000 is 1000 GDD, 12345 is 1.2345
 * GDD). Sizes come from the TYPICAL_* and LARGE_* macros; raising one of those is the intended
 * way to scale a fixture, and the only bound is the static buffers those macros also size --
 * except wire_body_json and wire_confirmed_json, which are fixed at 4096 bytes and are filled
 * later, by report_sizes().
 *
 * Takes no arguments, returns nothing, and is called once from main() before the timer rows.
 * One step can fail: if encoding the body into `encoded_body` fails -- it needs the body under
 * 512 bytes and the workspace sufficient -- wire_confirmed.transaction.body_bytes stays empty.
 * That failure goes through bench_fail(), so the run ends with a nonzero status, and it clears
 * have_encoded_body, so the rows that would have decoded that body are skipped rather than run
 * against a fixture that was never built. Everything not resting on the body still runs: a
 * table with one line accounted for as skipped says more than no table at all.
 */
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

  // the wire pair carries the same payload as the typical runtime transfer, so the two views
  // are compared on the same data rather than on two different transactions
  grdw_transaction_body_init(&wire_body);
  wire_body.created_at.seconds = 1700000000;
  wire_body.created_at.nanos = 2912;
  wire_body.transaction_type = GRDT_TRANSACTION_TRANSFER;
  wire_body.type = GRDT_CROSS_GROUP_LOCAL;
  ramp(wire_body.transfer.sender.pubkey, SIGN_PUBLIC_KEY_SIZE, 0x10);
  wire_body.transfer.sender.amount = 12345;
  memcpy(wire_body.transfer.sender.community_uuid, community_uuid, HOSTMEM_UUID_BINARY_SIZE);
  ramp(wire_body.transfer.recipient, SIGN_PUBLIC_KEY_SIZE, 0x40);
  wire_body.memos = typical_memos;
  wire_body.memos_count = 1;

  grdw_confirmed_transaction_init(&wire_confirmed);
  wire_confirmed.id = 121;
  wire_confirmed.confirmed_at.seconds = 1700000060;
  memcpy(wire_confirmed.running_hash, tx_typical.tx_running_hash, GENERIC_HASH_SIZE);
  wire_confirmed.ledger_anchor.type = GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_ID;
  wire_confirmed.ledger_anchor.id = 4711;
  wire_confirmed.balance_derivation = GRDT_BALANCE_DERIVATION_NODE;
  wire_confirmed.account_balances = typical_balances;
  wire_confirmed.account_balances_count = TYPICAL_BALANCES;
  wire_confirmed.transaction.sig_map = typical_signatures;
  wire_confirmed.transaction.sig_map_count = TYPICAL_SIGNATURES;
  // a real encoded body: the decoding rows below have to decode something, and encoding it here
  // keeps the benchmark honest about what a body of this shape actually costs
  static uint8_t encoded_body[512];
  _Alignas(8) static uint8_t encode_workspace[8192];
  {
    hostmem_memory_block destination = {encoded_body, sizeof(encoded_body)};
    hostmem_memory_block workspace = {encode_workspace, sizeof(encode_workspace)};
    int final_size = 0;
    if (HOSTMEM_SUCCESS ==
        grdw_transaction_body_encode(&destination, &final_size, &wire_body, &workspace)) {
      wire_confirmed.transaction.body_bytes.data = encoded_body;
      wire_confirmed.transaction.body_bytes.size = (uint32_t)final_size;
      have_encoded_body = true;
    } else {
      // without a body the decoding rows below have nothing to decode: the failure goes to the
      // exit status here, and have_encoded_body keeps those rows from running on a fixture that
      // was never built
      bench_fail("body encode");
    }
  }
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
 *
 * @param tx          the transaction to render; must outlive the call, along with everything
 *                    its array members point at.
 * @param format      GRDM_JSON_COMPACT or GRDM_JSON_PRETTY.
 * @param step_count  renders to perform. Values <= 0 perform none and leave both chains
 *                    untouched, which bench_step() reports as a zero per step figure.
 *
 * Returns nothing -- bench_step() takes a void(int) and has no status to pass on -- so a
 * failure goes through bench_fail() instead: the abort line names it, the row's total drops far
 * below its neighbours, and main() ends the process with a nonzero status so a runner sees the
 * partial table for what it is. Both chains are left reset on the normal path;
 * on the abort path the failing render's allocations are still in them, and the next row's
 * first reset clears them.
 */
static void render_repeatedly(
    const grdr_complete_transaction *tx, grdm_json_format format, int step_count
) {
  hostmem_memory_block json = {0};
  for (int i = 0; i < step_count; ++i) {
    if (HOSTMEM_SUCCESS != grdm_complete_transaction_to_json(&json, tx, format, &work, &result)) {
      bench_fail("render");
      return;
    }
    hostmem_multi_arena_reset(&work);
    hostmem_multi_arena_reset(&result);
  }
}

/*
 * The rows below all match the void(int) shape bench_step() calls, and all share one contract:
 *
 *   @param step_count  operations to perform. Values <= 0 perform none, which bench_step()
 *                      reports as a zero per step figure rather than dividing by zero. The
 *                      upper bound is what the run should take: the work is O(step_count),
 *                      the loop counter is `int`, and STEP_COUNT -- which every row passes --
 *                      is far below INT_MAX, so the counter cannot overflow at the sizes used
 *                      here.
 *
 * None of them return a value: bench_step() takes a void(int). A failed call calls bench_fail()
 * and returns early, so the row is reported over fewer operations than its step count claims --
 * visible as a total well under the neighbouring rows, preceded by the abort line, and carried
 * out of main() as a nonzero exit status. They all render into the shared `work` and `result`
 * chains and reset both after each pass, so they leave no state behind for the next row except
 * on that abort path.
 *
 * Each takes its input from the file-scope fixtures rather than from arguments, which is what
 * lets them share the one signature bench_step() accepts.
 */

/** @brief The wire transaction body, run through the same reset cycle as the rows above. */
static void bench_wire_body(int step_count) {
  hostmem_memory_block json = {0};
  for (int i = 0; i < step_count; ++i) {
    if (HOSTMEM_SUCCESS !=
        grdm_transaction_body_to_json(&json, &wire_body, GRDM_JSON_COMPACT, &work, &result)) {
      bench_fail("render");
      return;
    }
    hostmem_multi_arena_reset(&work);
    hostmem_multi_arena_reset(&result);
  }
}

/** @brief A gradido transaction, its body left as the hex string it arrives as. */
static void bench_wire_gradido(int step_count) {
  hostmem_memory_block json = {0};
  for (int i = 0; i < step_count; ++i) {
    if (HOSTMEM_SUCCESS != grdm_gradido_transaction_to_json(
                               &json, &wire_confirmed.transaction, GRDM_JSON_COMPACT, &work, &result
                           )) {
      bench_fail("render");
      return;
    }
    hostmem_multi_arena_reset(&work);
    hostmem_multi_arena_reset(&result);
  }
}

/**
 * @brief The same gradido transaction, with the body decoded into fields first.
 *
 * The difference against bench_wire_gradido() is what the pbtools decode plus the larger
 * output cost; PB_WORKSPACE_SIZE bounds the scratch that decode may use.
 */
static void bench_wire_gradido_decoded(int step_count) {
  hostmem_memory_block json = {0};
  for (int i = 0; i < step_count; ++i) {
    if (HOSTMEM_SUCCESS !=
        grdm_gradido_transaction_with_body_to_json(
            &json, &wire_confirmed.transaction, GRDM_JSON_COMPACT, PB_WORKSPACE_SIZE, &work, &result
        )) {
      bench_fail("render");
      return;
    }
    hostmem_multi_arena_reset(&work);
    hostmem_multi_arena_reset(&result);
  }
}

/** @brief The confirmed transaction with its body decoded, against the row below. */
static void bench_wire_confirmed_decoded(int step_count) {
  hostmem_memory_block json = {0};
  for (int i = 0; i < step_count; ++i) {
    if (HOSTMEM_SUCCESS !=
        grdm_confirmed_transaction_with_body_to_json(
            &json, &wire_confirmed, GRDM_JSON_COMPACT, PB_WORKSPACE_SIZE, &work, &result
        )) {
      bench_fail("render");
      return;
    }
    hostmem_multi_arena_reset(&work);
    hostmem_multi_arena_reset(&result);
  }
}

/** @brief The confirmed transaction, body left as hex: the full wire view of tx_typical. */
static void bench_wire_confirmed(int step_count) {
  hostmem_memory_block json = {0};
  for (int i = 0; i < step_count; ++i) {
    if (HOSTMEM_SUCCESS != grdm_confirmed_transaction_to_json(
                               &json, &wire_confirmed, GRDM_JSON_COMPACT, &work, &result
                           )) {
      bench_fail("render");
      return;
    }
    hostmem_multi_arena_reset(&work);
    hostmem_multi_arena_reset(&result);
  }
}

/**
 * @brief The way back: parse the text the writing rows produced.
 *
 * Reads wire_body_json, which report_sizes() filled from an actual render, so the row prices
 * the parse of exactly the text this library writes rather than of a hand-made sample. If
 * report_sizes() never ran, that buffer is empty and every pass fails on the first call.
 */
static void bench_read_wire_body(int step_count) {
  for (int i = 0; i < step_count; ++i) {
    grdw_transaction_body body;
    grdw_transaction_body_init(&body);
    if (HOSTMEM_SUCCESS != grdm_transaction_body_from_json(
                               &body, wire_body_json, wire_body_json_size, NULL, &work, &result
                           )) {
      bench_fail("read");
      return;
    }
    hostmem_multi_arena_reset(&work);
    hostmem_multi_arena_reset(&result);
  }
}

/** @brief The same, for the confirmed transaction; reads wire_confirmed_json. */
static void bench_read_wire_confirmed(int step_count) {
  for (int i = 0; i < step_count; ++i) {
    grdw_confirmed_transaction tx;
    grdw_confirmed_transaction_init(&tx);
    if (HOSTMEM_SUCCESS !=
        grdm_confirmed_transaction_from_json(
            &tx, wire_confirmed_json, wire_confirmed_json_size, NULL, &work, &result
        )) {
      bench_fail("read");
      return;
    }
    hostmem_multi_arena_reset(&work);
    hostmem_multi_arena_reset(&result);
  }
}

/*
 * Six wrappers, one per (shape, format) pair. They exist only to bind a fixture and a format
 * to the void(int) signature bench_step() takes; @p n is render_repeatedly()'s step_count and
 * carries its contract unchanged.
 */
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
 *
 * Uses local chains rather than the shared ones, so it leaves `work` and `result` untouched.
 * Each pass releases what it opened, the abort path included: the `|| ` chain short-circuits,
 * so a pass can fail with one chain open and the other still zeroed, and both are handed to
 * hostmem_multi_arena_release() -- it takes a zeroed chain untouched. Nothing is left behind
 * before the failure propagates. Otherwise the shared contract above applies.
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
      // whatever the short-circuit above managed to open still has to go back: a chain the
      // first init never reached is still zeroed, and release takes that untouched
      hostmem_multi_arena_release(&cold_work);
      hostmem_multi_arena_release(&cold_result);
      bench_fail("render");
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
 *
 * The chains are built over borrowed_work, borrowed_result and borrowed_bookkeeping, so the
 * measured cost of one pass is bounded by those fixed sizes: a borrowed chain that runs out
 * cannot grow, it fails, and the row aborts. Setup happens once, outside the loop, and is not
 * part of what the per step figure divides. Both abort paths release the two chains and then
 * the bookkeeping they drew their descriptors from, in that order, before the failure
 * propagates -- the same partial-setup rule as bench_cold_chains(), since the setup condition
 * short-circuits too. Otherwise the shared contract above applies.
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
    // the same short-circuit rule as above: release all three, zeroed or not, then fail
    hostmem_multi_arena_release(&borrowed_work_chain);
    hostmem_multi_arena_release(&borrowed_result_chain);
    hostmem_release(&bookkeeping);
    bench_fail("borrowed setup");
    return;
  }

  for (int i = 0; i < step_count; ++i) {
    if (HOSTMEM_SUCCESS !=
        grdm_complete_transaction_to_json(
            &json, &tx_typical, GRDM_JSON_COMPACT, &borrowed_work_chain, &borrowed_result_chain
        )) {
      hostmem_multi_arena_release(&borrowed_work_chain);
      hostmem_multi_arena_release(&borrowed_result_chain);
      hostmem_release(&bookkeeping);
      bench_fail("render");
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
 * whole difference between the two designs on the time axis; the memory axis is in the header,
 * json_from_runtime.h, where the copy earns a result chain holding the text and nothing else.
 *
 * The copy is TYPICAL_TEXT_SIZE + 1 bytes, which is what the mapping actually asks for:
 * grdm_json_writer_finish() clones `length + 1` and then reports `length` as json.size, so the
 * terminator is allocated and copied but not counted. Cloning TYPICAL_TEXT_SIZE alone would
 * price one byte less than the copy this row exists to price.
 *
 * The source is a static buffer of zeroes, not a rendered transaction: only the byte count
 * reaches hostmem_multi_arena_clone(), so its contents cannot change the figure -- but it is
 * sized for the full request, since the clone reads every byte it is asked for.
 * TYPICAL_TEXT_SIZE therefore has to track the compact size the size rows report for the
 * typical transfer, or this row prices a copy of the wrong length. A failed clone leaves the
 * shared result chain unreset, which the next row's reset clears. Otherwise the shared
 * contract above applies.
 */
static void bench_clone_reference(int step_count) {
  static uint8_t text[TYPICAL_TEXT_SIZE + 1];
  uint8_t *copy = NULL;
  for (int i = 0; i < step_count; ++i) {
    if (HOSTMEM_SUCCESS !=
        hostmem_multi_arena_clone(&copy, text, TYPICAL_TEXT_SIZE + 1, &result)) {
      bench_fail("clone");
      return;
    }
    hostmem_multi_arena_reset(&result);
  }
}

// ****************** what each shape comes to *********************************************

/**
 * @brief One render, to report the text size a row's per step figure belongs to.
 *
 * @param tx      the transaction to render, not null.
 * @param format  GRDM_JSON_COMPACT or GRDM_JSON_PRETTY.
 *
 * @return the rendered size in bytes, not counting the terminating zero, or 0 if the render
 *         failed. A successful render is never 0 bytes, so the two cases stay distinguishable
 *         in the printed table -- but a caller does not have to check: the failure has already
 *         gone through bench_fail() and will reach the exit status on its own.
 *
 * Untimed. Resets both shared chains on success; on failure it returns with the failed
 * render's allocations still in them, and the next reset clears those.
 */
static uint32_t measure_size(const grdr_complete_transaction *tx, grdm_json_format format) {
  hostmem_memory_block json = {0};
  if (HOSTMEM_SUCCESS != grdm_complete_transaction_to_json(&json, tx, format, &work, &result)) {
    bench_fail("size render");
    return 0;
  }
  uint32_t size = json.size;
  hostmem_multi_arena_reset(&work);
  hostmem_multi_arena_reset(&result);
  return size;
}

/**
 * @brief Print the text size of every shape, and capture the text the reading rows parse.
 *
 * Takes no arguments and returns nothing. Beyond printing, it fills wire_body_json and
 * wire_confirmed_json from real renders, which bench_read_wire_body() and
 * bench_read_wire_confirmed() then parse -- so it must run before them, as main() arranges.
 * Both buffers are 4096 bytes and are copied into with `json.size + 1` bytes for the
 * terminator. A render that does not fit is refused rather than copied, so growing a fixture
 * past what they hold costs a named failure and a nonzero exit rather than a silent write past
 * the end of a static buffer -- the reading row that would have parsed the text then finds its
 * buffer empty and aborts on its first pass.
 *
 * A row whose render fails is skipped -- its line is not printed and, for the two captured
 * texts, the corresponding buffer stays empty and its reading row aborts on the first pass --
 * and every one of those skips goes through bench_fail(), so a size table with a line missing
 * always comes with a nonzero exit status rather than passing as complete.
 */
static void report_sizes() {
  printf("\ntext size per shape (compact / pretty, bytes)\n");
  printf(
      "%-*s %12" PRIu32 "  %10" PRIu32 "\n", BENCH_NAME_WIDTH, "  minimal, no arrays",
      measure_size(&tx_minimal, GRDM_JSON_COMPACT), measure_size(&tx_minimal, GRDM_JSON_PRETTY)
  );
  printf(
      "%-*s %12" PRIu32 "  %10" PRIu32 "\n", BENCH_NAME_WIDTH, "  typical transfer",
      measure_size(&tx_typical, GRDM_JSON_COMPACT), measure_size(&tx_typical, GRDM_JSON_PRETTY)
  );
  printf(
      "%-*s %12" PRIu32 "  %10" PRIu32 "\n", BENCH_NAME_WIDTH, "  large, 4 KB body_bytes",
      measure_size(&tx_large, GRDM_JSON_COMPACT), measure_size(&tx_large, GRDM_JSON_PRETTY)
  );

  hostmem_memory_block json = {0};
  if (HOSTMEM_SUCCESS ==
      grdm_transaction_body_to_json(&json, &wire_body, GRDM_JSON_COMPACT, &work, &result)) {
    printf("%-*s %12" PRIu32 "\n", BENCH_NAME_WIDTH, "  wire transaction body", json.size);
    // kept for the reading rows, which parse exactly what the writing rows produce. The
    // comparison is against the buffer rather than one less than it so that `size + 1` cannot
    // wrap on the way in: a size that leaves no room for the terminator is already too large.
    if (json.size >= sizeof(wire_body_json)) {
      bench_fail("body text capture");
    } else {
      wire_body_json_size = json.size;
      memcpy(wire_body_json, json.data, json.size + 1);
    }
  } else {
    bench_fail("size render");
  }
  hostmem_multi_arena_reset(&work);
  hostmem_multi_arena_reset(&result);
  if (HOSTMEM_SUCCESS == grdm_gradido_transaction_to_json(
                             &json, &wire_confirmed.transaction, GRDM_JSON_COMPACT, &work, &result
                         )) {
    printf("%-*s %12" PRIu32 "\n", BENCH_NAME_WIDTH, "  wire gradido transaction", json.size);
  } else {
    bench_fail("size render");
  }
  hostmem_multi_arena_reset(&work);
  hostmem_multi_arena_reset(&result);
  if (HOSTMEM_SUCCESS == grdm_confirmed_transaction_to_json(
                             &json, &wire_confirmed, GRDM_JSON_COMPACT, &work, &result
                         )) {
    printf("%-*s %12" PRIu32 "\n", BENCH_NAME_WIDTH, "  wire confirmed transaction", json.size);
    if (json.size >= sizeof(wire_confirmed_json)) {
      bench_fail("confirmed text capture");
    } else {
      wire_confirmed_json_size = json.size;
      memcpy(wire_confirmed_json, json.data, json.size + 1);
    }
  } else {
    bench_fail("size render");
  }
  hostmem_multi_arena_reset(&work);
  hostmem_multi_arena_reset(&result);
  if (!have_encoded_body) {
    bench_skipped("  ... with the body decoded", "skipped, no body was encoded");
  } else if (HOSTMEM_SUCCESS ==
             grdm_confirmed_transaction_with_body_to_json(
                 &json, &wire_confirmed, GRDM_JSON_COMPACT, PB_WORKSPACE_SIZE, &work, &result
             )) {
    printf("%-*s %12" PRIu32 "\n", BENCH_NAME_WIDTH, "  ... with the body decoded", json.size);
  } else {
    bench_fail("size render");
  }
  hostmem_multi_arena_reset(&work);
  hostmem_multi_arena_reset(&result);
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
    // whichever of the two came up has to go back before the run gives up on the rest
    hostmem_multi_arena_release(&work);
    hostmem_multi_arena_release(&result);
    bench_fail("chain setup");
    return 1;
  }
  bench_prepared(time_used);
  report_sizes();

  bench_section("complete transaction to json, compact");
  bench_step(bench_minimal_compact, STEP_COUNT, "  minimal, no arrays", "transaction");
  bench_step(bench_typical_compact, STEP_COUNT, "  typical transfer", "transaction");
  bench_step(bench_large_compact, STEP_COUNT, "  large, 4 KB body_bytes", "transaction");

  bench_section("complete transaction to json, pretty");
  bench_step(bench_minimal_pretty, STEP_COUNT, "  minimal, no arrays", "transaction");
  bench_step(bench_typical_pretty, STEP_COUNT, "  typical transfer", "transaction");
  bench_step(bench_large_pretty, STEP_COUNT, "  large, 4 KB body_bytes", "transaction");

  bench_section("wire view, compact");
  bench_step(bench_wire_body, STEP_COUNT, "  transaction body", "body");
  bench_step(bench_wire_gradido, STEP_COUNT, "  gradido transaction, body as hex", "transaction");
  if (have_encoded_body) {
    bench_step(
        bench_wire_gradido_decoded, STEP_COUNT, "  gradido transaction, body decoded",
        "transaction"
    );
  } else {
    bench_skipped("  gradido transaction, body decoded", "skipped, no body was encoded");
  }
  bench_step(
      bench_wire_confirmed, STEP_COUNT, "  confirmed transaction, body as hex", "transaction"
  );
  if (have_encoded_body) {
    bench_step(
        bench_wire_confirmed_decoded, STEP_COUNT, "  confirmed transaction, body decoded",
        "transaction"
    );
  } else {
    bench_skipped("  confirmed transaction, body decoded", "skipped, no body was encoded");
  }

  bench_section("wire view, read back");
  bench_step(bench_read_wire_body, STEP_COUNT, "  transaction body", "body");
  bench_step(bench_read_wire_confirmed, STEP_COUNT, "  confirmed transaction", "transaction");

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
  // one failed row anywhere above makes the whole table partial, and the status says so
  return bench_failure;
}
