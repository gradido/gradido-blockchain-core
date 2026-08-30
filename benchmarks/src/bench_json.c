#include "arnm/arena.h"
#include "arnm/mono_timer.h"
#include "bench_report.h"
#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"
#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/data/wire/ledger_anchor.h"
#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "gradido_blockchain_core/mapping/json_from_runtime.h"
#include "gradido_blockchain_core/mapping/runtime_from_json.h"
#include "gradido_blockchain_core/mapping/runtime_from_wire.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_blockchain_core/types/cross_group.h"
#include "gradido_blockchain_core/types/ledger_anchor.h"
#include "gradido_blockchain_core/types/memo_key.h"
#include "gradido_blockchain_core/types/transaction.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * What one transaction costs on its way into text and back.
 *
 * The same transfer is carried in three shapes -- protobuf, minified JSON, pretty JSON -- and
 * every row below moves it between two of them. The protobuf row is not a competitor but a
 * ruler: it is the representation this library already had, it walks the same
 * grdm_complete_transaction_from_wire() at the end, and it draws its runtime arena from the
 * host exactly as the JSON reader does. So "from json" and "from protobuf" are directly
 * comparable, and what separates them is the parse and the hex, not the bookkeeping around it.
 *
 * "to json" is a different shape of work again and not comparable to either: it draws only from
 * the caller's arena, where a read also opens an arena of its own from the host for the
 * transaction it fills. Which of the two comes out ahead is a question for the numbers and not
 * for this comment -- on the large fixture the write is the slower one, because every byte of
 * memo and body is hexed into scratch and then copied a second time into the document.
 *
 * Two fixtures, because the two halves of the cost scale differently. A transaction's fixed
 * fields -- keys, hashes, a uuid, the enumerations -- are the same handful of bytes whatever
 * else it carries, while the memos and the body bytes are as long as they are, and hex charges
 * two characters for each of them. The small fixture is nearly all fixed fields; the large one
 * is nearly all payload, and the gap between their per-step figures is where the hex lives.
 */

#define MEMO_BYTES 256
#define BODY_BUFFER_SIZE 4096
#define PROTOBUF_BUFFER_SIZE (64u * 1024u)
/* one transaction at a time, reset between steps -- the pretty form of the wide fixture is the
   tallest thing that ever stands in here */
#define BENCH_ARENA_SIZE (4u * 1024u * 1024u)
/* holds the wire structures, the encoded buffers and both documents of every fixture, for the
   whole run */
#define PREPARE_ARENA_SIZE (2u * 1024u * 1024u)

static const uint8_t communityUuid[ARNM_UUID_BINARY_SIZE] = {0x01, 0x9e, 0x2c, 0x31, 0xa3, 0x03,
                                                             0x75, 0xc0, 0x94, 0x1e, 0xf3, 0x5c,
                                                             0x59, 0xe4, 0xf9, 0x78};

typedef struct fixture {
  /* short enough for the name column of the size table; the heading below says the rest */
  const char *label;
  const char *title;
  /* the wire half, kept because the protobuf row decodes into it again every step */
  grdw_transaction_body body;
  grdw_confirmed_transaction confirmed;
  uint8_t body_buffer[BODY_BUFFER_SIZE];
  uint8_t protobuf_buffer[PROTOBUF_BUFFER_SIZE];
  arnm_memory_block protobuf;
  /* the three shapes the rows move between */
  grdr_complete_transaction runtime;
  arnm_memory_block json_minified;
  arnm_memory_block json_pretty;
} fixture;

static fixture small_transfer;
static fixture large_transfer;
static fixture wide_transfer;
/* bench_step() hands its function nothing but a step count, so the fixture a row runs on is
   named here rather than passed */
static const fixture *current = NULL;

static arnm prepare_arena;
static arnm bench_arena;

/* Deterministic filler: the same bytes every run, so two runs on one machine compare. */
static void fill_bytes(uint8_t *data, uint32_t size, uint8_t seed) {
  for (uint32_t i = 0; i < size; ++i) { data[i] = (uint8_t)(seed + i * 7u); }
}

/* A benchmark that silently measured a refusal would print a very fast row and mean nothing. */
static void must(arnm_result result, const char *what) {
  if (ARNM_SUCCESS != result && ARNM_WARNING_ARENA_MEMORY_NOT_RECLAIMED != result) {
    printf("bench_json: %s failed: %s\n", what, grd_result_to_string(result));
    exit(1);
  }
}

/**
 * Builds one fixture in all three shapes.
 *
 * @param balances   account balances the confirmed transaction settles
 * @param memos      encrypted memos of MEMO_BYTES each, carried inside the body
 * @param signatures signature pairs over the encoded body
 */
static void prepare_fixture(
    fixture *f,
    const char *label,
    const char *title,
    uint8_t balances,
    uint8_t memos,
    uint8_t signatures
) {
  f->label = label;
  f->title = title;

  grdw_transaction_body_init(&f->body);
  f->body.created_at.seconds = 1749999000;
  f->body.created_at.nanos = 42;
  f->body.transaction_type = GRDT_TRANSACTION_TRANSFER;
  f->body.type = GRDT_CROSS_GROUP_LOCAL;
  fill_bytes(f->body.transfer.sender.pubkey, SIGN_PUBLIC_KEY_SIZE, 0x01);
  f->body.transfer.sender.amount = 1234567890;
  memcpy(f->body.transfer.sender.community_uuid, communityUuid, ARNM_UUID_BINARY_SIZE);
  fill_bytes(f->body.transfer.recipient, SIGN_PUBLIC_KEY_SIZE, 0x02);

  if (memos) {
    must(grdw_transaction_body_reserve_memos(&f->body, memos, &prepare_arena), "reserve memos");
    for (uint8_t i = 0; i < memos; ++i) {
      grdw_encrypted_memo memo;
      memo.type = GRDT_MEMO_KEY_COMMUNITY_SECRET;
      must(arnm_memory_block_alloc(&memo.memo, MEMO_BYTES, &prepare_arena), "alloc memo");
      fill_bytes(memo.memo.data, MEMO_BYTES, (uint8_t)(0x50 + i));
      must(grdw_transaction_body_copy_memo(&f->body, &memo, i, &prepare_arena), "copy memo");
    }
  }

  /* the body's own encoding is the payload the signatures cover and the block the runtime
     transaction keeps as body_bytes -- so it has to exist before anything else is assembled */
  arnm_memory_block body_dst = {f->body_buffer, BODY_BUFFER_SIZE};
  int body_size = 0;
  must(
      grdw_transaction_body_encode(&body_dst, &body_size, &f->body, &prepare_arena), "encode body"
  );

  grdw_confirmed_transaction_init(&f->confirmed);
  f->confirmed.id = 4711;
  f->confirmed.confirmed_at.seconds = 1750000000;
  f->confirmed.confirmed_at.nanos = 123456789;
  fill_bytes(f->confirmed.running_hash, GENERIC_HASH_SIZE, 0x20);
  f->confirmed.ledger_anchor.type = GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_ID;
  f->confirmed.ledger_anchor.id = 987654321;
  f->confirmed.balance_derivation = GRDT_BALANCE_DERIVATION_NODE;

  must(
      grdw_confirmed_transaction_reserve_account_balances(&f->confirmed, balances, &prepare_arena),
      "reserve account balances"
  );
  for (uint8_t i = 0; i < balances; ++i) {
    grdw_account_balance balance;
    fill_bytes(balance.pubkey, SIGN_PUBLIC_KEY_SIZE, (uint8_t)(0x30 + i));
    balance.balance = -123456 + i;
    memcpy(balance.community_uuid, communityUuid, ARNM_UUID_BINARY_SIZE);
    must(
        grdw_confirmed_transaction_copy_account_balance(&f->confirmed, &balance, i),
        "copy account balance"
    );
  }

  grdw_gradido_transaction *transaction = &f->confirmed.transaction;
  grdw_gradido_transaction_init(transaction);
  transaction->body_bytes.data = f->body_buffer;
  transaction->body_bytes.size = (uint32_t)body_size;
  must(
      grdw_gradido_transaction_reserve_sig_map(transaction, signatures, &prepare_arena),
      "reserve sig map"
  );
  for (uint8_t i = 0; i < signatures; ++i) {
    grdw_signature_pair pair;
    fill_bytes(pair.public_key, SIGN_PUBLIC_KEY_SIZE, (uint8_t)(0x60 + i));
    fill_bytes(pair.signature, SIGN_SIGNATURE_SIZE, (uint8_t)(0x70 + i));
    must(grdw_gradido_transaction_copy_sig_map(transaction, &pair, i), "copy sig map");
  }

  arnm_memory_block protobuf_dst = {f->protobuf_buffer, PROTOBUF_BUFFER_SIZE};
  int protobuf_size = 0;
  must(
      grdw_confirmed_transaction_encode(
          &protobuf_dst, &protobuf_size, &f->confirmed, &prepare_arena
      ),
      "encode confirmed transaction"
  );
  f->protobuf.data = f->protobuf_buffer;
  f->protobuf.size = (uint32_t)protobuf_size;

  grdr_complete_transaction_init(&f->runtime);
  must(
      grdm_complete_transaction_from_wire(&f->runtime, &f->body, &f->confirmed, communityUuid),
      "map wire to runtime"
  );

  /* the two documents live in the prepare arena, so a step that reads one is not also timing
     the write that produced it */
  must(
      grdm_json_from_complete_transaction(
          &f->json_minified, &f->runtime, &prepare_arena, ARNM_JSON_WRITE_DEFAULT
      ),
      "write minified json"
  );
  must(
      grdm_json_from_complete_transaction(
          &f->json_pretty, &f->runtime, &prepare_arena, ARNM_JSON_WRITE_PRETTY
      ),
      "write pretty json"
  );
}

// ********** the rows *********************************************************************

static void step_to_json_minified(int step_count) {
  for (int i = 0; i < step_count; ++i) {
    arnm_memory_block text = {NULL, 0};
    must(
        grdm_json_from_complete_transaction(
            &text, &current->runtime, &bench_arena, ARNM_JSON_WRITE_DEFAULT
        ),
        "write minified json"
    );
    arnm_reset(&bench_arena);
  }
}

static void step_to_json_pretty(int step_count) {
  for (int i = 0; i < step_count; ++i) {
    arnm_memory_block text = {NULL, 0};
    must(
        grdm_json_from_complete_transaction(
            &text, &current->runtime, &bench_arena, ARNM_JSON_WRITE_PRETTY
        ),
        "write pretty json"
    );
    arnm_reset(&bench_arena);
  }
}

static void step_from_json(int step_count) {
  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);
  for (int i = 0; i < step_count; ++i) {
    must(
        grdm_complete_transaction_from_json(
            &tx, (const char *)current->json_minified.data, current->json_minified.size - 1u,
            &bench_arena
        ),
        "read minified json"
    );
    arnm_reset(&bench_arena);
  }
  grdr_complete_transaction_release(&tx);
}

static void step_from_json_pretty(int step_count) {
  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);
  for (int i = 0; i < step_count; ++i) {
    must(
        grdm_complete_transaction_from_json(
            &tx, (const char *)current->json_pretty.data, current->json_pretty.size - 1u,
            &bench_arena
        ),
        "read pretty json"
    );
    arnm_reset(&bench_arena);
  }
  grdr_complete_transaction_release(&tx);
}

/* The ruler: the same transaction arriving the way it always did. Two decodes and the same
   mapping the JSON reader ends with, so the difference between this row and "from json,
   minified" is what the readable representation costs. */
static void step_from_protobuf(int step_count) {
  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);
  for (int i = 0; i < step_count; ++i) {
    grdw_confirmed_transaction confirmed;
    grdw_confirmed_transaction_init(&confirmed);
    must(
        grdw_confirmed_transaction_decode(&confirmed, &current->protobuf, &bench_arena),
        "decode confirmed transaction"
    );
    grdw_transaction_body body;
    grdw_transaction_body_init(&body);
    must(
        grdw_transaction_body_decode(&body, &confirmed.transaction.body_bytes, &bench_arena),
        "decode transaction body"
    );
    must(
        grdm_complete_transaction_from_wire(&tx, &body, &confirmed, communityUuid),
        "map wire to runtime"
    );
    arnm_reset(&bench_arena);
  }
  grdr_complete_transaction_release(&tx);
}

static void report_sizes(const fixture *f) {
  printf(
      "%-*s %11u  %11u  %11u\n", BENCH_NAME_WIDTH, f->label, f->protobuf.size,
      f->json_minified.size - 1u, f->json_pretty.size - 1u
  );
}

static void run_fixture(const fixture *f, int step_count) {
  current = f;
  bench_section(f->title);
  bench_step(step_to_json_minified, step_count, "  to json, minified", "transaction");
  bench_step(step_to_json_pretty, step_count, "  to json, pretty", "transaction");
  bench_step(step_from_json, step_count, "  from json, minified", "transaction");
  bench_step(step_from_json_pretty, step_count, "  from json, pretty", "transaction");
  bench_step(step_from_protobuf, step_count, "  from protobuf, for scale", "transaction");
}

int main(void) {
  arnm_mono_timer_init();
  arnm_mono_timer timeUsed;

  arnm_mono_timer_reset(&timeUsed);
  must(arnm_init_arena(&prepare_arena, PREPARE_ARENA_SIZE), "open prepare arena");
  must(arnm_init_arena(&bench_arena, BENCH_ARENA_SIZE), "open bench arena");
  prepare_fixture(
      &small_transfer, "  small transfer", "small transfer -- 1 balance, no memo, 1 signature", 1,
      0, 1
  );
  prepare_fixture(
      &large_transfer, "  large transfer", "large transfer -- 8 balances, 4 memos, 3 signatures", 8,
      4, 3
  );
  /* Not a transaction anyone will see: it exists to isolate one cost. Its payload is as small as
     the small fixture's, and everything above that is array elements -- so the gap between this
     row and the small one is what walking and reading 400 little objects is worth, with the hex
     of a memo nowhere near it. */
  prepare_fixture(
      &wide_transfer, "  wide transfer", "wide transfer -- 200 balances, no memo, 200 signatures",
      200, 0, 200
  );
  bench_prepared(timeUsed);

  /* Chosen so a debug build stays in the same minute as the other bench_* binaries when
     run_all.sh prints them in a row; in a ReleaseFast build the whole run is a quarter second.
     Every row still moves thousands of kilobytes, which is far past what timer noise reaches. */
  const int stepCount = 10000;

  bench_section("one transaction, three representations, in bytes");
  printf("%-*s %11s  %11s  %11s\n", BENCH_NAME_WIDTH, "", "protobuf", "json", "json pretty");
  report_sizes(&small_transfer);
  report_sizes(&large_transfer);
  report_sizes(&wide_transfer);

  run_fixture(&small_transfer, stepCount);
  run_fixture(&large_transfer, stepCount);
  run_fixture(&wide_transfer, stepCount);

  bench_total(timeUsed, stepCount, "transaction");

  grdr_complete_transaction_release(&small_transfer.runtime);
  grdr_complete_transaction_release(&large_transfer.runtime);
  grdr_complete_transaction_release(&wide_transfer.runtime);
  arnm_release(&bench_arena);
  arnm_release(&prepare_arena);
  return 0;
}
