#include "gradido_blockchain_core/mapping/json_from_runtime.h"

#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/data/timestamp.h"
#include "gradido_blockchain_core/data/unit.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/hiero.h"
#include "gradido_blockchain_core/data/wire/ledger_anchor.h"
#include "gradido_blockchain_core/types/address.h"
#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_blockchain_core/types/cross_group.h"
#include "gradido_blockchain_core/types/ledger_anchor.h"
#include "gradido_blockchain_core/types/memo_key.h"
#include "gradido_blockchain_core/types/transaction.h"
#include "hostmem/converter.h"
#include "hostmem/memory_block.h"
#include "hostmem/multi_arena.h"
#include "hostmem/result.h"

#include "yyjson.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief Characters grdd_unit_to_string() can write at precision 4, terminator included.
 *
 * The widest value is INT64_MIN, twenty digits, and the sign and the decimal point are two
 * more: `-922337203685477.5808` is 21 characters. One byte for the terminator closes it.
 * A compile time bound, so the scratch buffer is sized once and never measured again.
 */
#define UNIT_STRING_CAPACITY 22

// ****************** the two chains, seen through yyjson's allocator ***********************
//
// yyjson asks for memory through three function pointers and a context. The context here is a
// hostmem_multi_arena, so every byte the rendering needs -- the document, the value pool, the
// writer's output buffer -- is drawn from the chain the caller opened, and malloc is never
// reached. A bump chain hands memory back in one motion rather than block by block, which is
// what shapes the three below.

static void *arena_malloc(void *ctx, size_t size) {
  hostmem_multi_arena *chain = (hostmem_multi_arena *)ctx;
  if (size > UINT32_MAX) { return NULL; }
  // NULL is how this interface says "no memory", so a zero sized request cannot answer with it
  // and asks for one byte instead -- which the arena rounds up to its alignment anyway
  uint8_t *buffer = NULL;
  if (HOSTMEM_SUCCESS != hostmem_multi_arena_alloc(&buffer, size ? (uint32_t)size : 1, chain)) {
    return NULL;
  }
  return buffer;
}

static void *arena_realloc(void *ctx, void *ptr, size_t old_size, size_t size) {
  if (!ptr) { return arena_malloc(ctx, size); }
  // the block already reaches that far; a bump allocator cannot give the difference back, and
  // the reservation stays what it was
  if (size <= old_size) { return ptr; }
  void *grown = arena_malloc(ctx, size);
  if (!grown) { return NULL; }
  memcpy(grown, ptr, old_size);
  return grown;
}

static void arena_free(void *ctx, void *ptr) {
  // Deliberately empty. hostmem_multi_arena_free() reclaims a block only while it is the tail
  // of its own arena, and it needs the size to do it -- a size this interface does not carry.
  // So nothing is attempted: the chain comes back whole at the caller's
  // hostmem_multi_arena_reset(), which is what the header promises.
  (void)ctx;
  (void)ptr;
}

/** @brief What every field needs to reach: the document being built and the scratch chain. */
typedef struct render_ctx {
  yyjson_mut_doc *doc;
  hostmem_multi_arena *work;
  /**
   * @brief The first refusal that was not the chain running dry, or HOSTMEM_SUCCESS.
   *
   * The value builders answer with NULL whatever went wrong, which leaves two causes that
   * read alike at the call site. A value that refused to be written as text records itself
   * here on the way out, so the code the caller finally sees names the field's own limit
   * rather than blaming the allocator for it.
   */
  hostmem_result failure;
} render_ctx;

/**
 * @brief The code a failed build reports.
 *
 * A recorded refusal if there was one; memory exhaustion otherwise, which is the only other
 * way a value can fail to appear.
 */
static hostmem_result build_failure(const render_ctx *ctx) {
  return HOSTMEM_SUCCESS != ctx->failure ? ctx->failure : HOSTMEM_ERROR_OUT_OF_MEMORY;
}

// ****************** binary and numeric fields, turned into text ***************************
//
// Each of these leaves its characters in the work chain and hands yyjson a view of them
// (yyjson_mut_strn does not copy). Both live in the same chain and are released together, so
// the second copy a copying variant would make would be paid for and never needed.

/**
 * @brief Hand yyjson a string it may write out without scanning it.
 *
 * Every string this file builds comes from a closed alphabet -- hex digits, the uuid
 * separator, digits with a sign and a decimal point -- so none of them can hold a quote, a
 * backslash, a control character or a byte that is not valid UTF-8. Saying so lets the writer
 * memcpy the string between its quotes instead of walking it character by character.
 *
 * The promise is the caller's to keep: mark nothing here that a transaction's own bytes reach
 * unencoded. Binary fields go out as hex, which is what makes the promise hold.
 */
static yyjson_mut_val *noesc(yyjson_mut_val *val) {
  yyjson_mut_set_str_noesc(val, true);
  return val;
}

/**
 * @brief @p size bytes from the work chain, to write characters into.
 *
 * The seam between two ways of reporting failure. hostmem answers with a result code and fills
 * an out parameter; the value builders below answer with the value and mean failure by NULL.
 * Crossing that once, here, is what lets those four read alike -- and it puts the one cast from
 * bytes to characters in a place whose name says why it is a cast to characters.
 *
 * @param[in,out] work Chain to draw from; not NULL.
 * @param[in]     size Bytes to reserve, terminator included where the caller writes one.
 * @return The buffer, or NULL when the chain could not open another arena. Not zeroed: it holds
 *         whatever the previous tenant left, and every caller writes before it reads.
 * @whisper Ground is set aside, and the characters settle into it
 */
static char *text_buffer(hostmem_multi_arena *work, uint32_t size) {
  uint8_t *buffer = NULL;
  if (HOSTMEM_SUCCESS != hostmem_multi_arena_alloc(&buffer, size, work)) { return NULL; }
  return (char *)buffer;
}

/** @brief Lowercase hex of @p size bytes, two characters each. NULL when @p size is 0. */
static yyjson_mut_val *hex_val(render_ctx *ctx, const uint8_t *data, uint32_t size) {
  if (!data || !size) { return NULL; }
  // two characters per byte and a terminator, counted in the uint32_t hostmem measures an
  // allocation in -- a block past this bound would wrap into a buffer too small to write into
  if (size > (UINT32_MAX - 1) / 2) {
    ctx->failure = HOSTMEM_ERROR_ARITHMETIC_OVERFLOW;
    return NULL;
  }
  char *buffer = text_buffer(ctx->work, size * 2 + 1);
  if (!buffer) { return NULL; }
  const hostmem_memory_block block = {(uint8_t *)data, size};
  if (HOSTMEM_SUCCESS != hostmem_binary_to_hex(buffer, &block)) { return NULL; }
  return noesc(yyjson_mut_strn(ctx->doc, buffer, (size_t)size * 2));
}

/** @brief The canonical 8-4-4-4-12 form of 16 bytes. */
static yyjson_mut_val *uuid_val(render_ctx *ctx, const uint8_t uuid[HOSTMEM_UUID_BINARY_SIZE]) {
  if (!uuid) { return NULL; }
  char *buffer = text_buffer(ctx->work, HOSTMEM_UUID_STRING_LENGTH + 1);
  if (!buffer) { return NULL; }
  hostmem_uuid_to_string(buffer, uuid);
  return noesc(yyjson_mut_strn(ctx->doc, buffer, HOSTMEM_UUID_STRING_LENGTH));
}

/** @brief `seconds.nanoseconds`, nanoseconds always nine digits. NULL when nanos is out of
 *         range, which is the one input grdd_timestamp_to_string() refuses. */
static yyjson_mut_val *timestamp_val(render_ctx *ctx, const grdd_timestamp *timestamp) {
  size_t size = grdd_timestamp_calculate_string_size(timestamp);
  if (!size) {
    ctx->failure = HOSTMEM_ERROR_ENCODE_FAILED;
    return NULL;
  }
  char *buffer = text_buffer(ctx->work, (uint32_t)size + 1);
  if (!buffer) { return NULL; }
  // the buffer is sized from the same figure this returns, so a short write says the value
  // changed underneath us rather than that the buffer was misjudged
  if (grdd_timestamp_to_string(buffer, size + 1, timestamp) != size) {
    ctx->failure = HOSTMEM_ERROR_ENCODE_FAILED;
    return NULL;
  }
  return noesc(yyjson_mut_strn(ctx->doc, buffer, size));
}

/** @brief Fixed-point GDD as a decimal string with four fractional digits.
 *
 *  Never a JSON number: the value is scaled by 10^4 and a double carries only 53 bits of
 *  mantissa, so amounts above 2^53 / 10^4 would come back changed from a reader that parses
 *  numbers as doubles -- which most of them do.
 */
static yyjson_mut_val *unit_val(render_ctx *ctx, grdd_unit value) {
  char *buffer = text_buffer(ctx->work, UNIT_STRING_CAPACITY);
  if (!buffer) { return NULL; }
  int written = grdd_unit_to_string(buffer, UNIT_STRING_CAPACITY, value, 4);
  // negative is a refusal; a figure reaching the capacity is the "this is what I would have
  // needed" answer, which means nothing was written
  if (written <= 0 || written >= UNIT_STRING_CAPACITY) {
    ctx->failure = HOSTMEM_ERROR_ENCODE_FAILED;
    return NULL;
  }
  return noesc(yyjson_mut_strn(ctx->doc, buffer, (size_t)written));
}

// ****************** composite fields **************************************************

static yyjson_mut_val *hiero_transaction_id_obj(
    render_ctx *ctx, const grdw_hiero_transaction_id *hiero
) {
  yyjson_mut_doc *doc = ctx->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  if (!obj) { return NULL; }

  yyjson_mut_val *valid_start = timestamp_val(ctx, &hiero->transactionValidStart);
  yyjson_mut_val *account = yyjson_mut_obj(doc);
  if (!valid_start || !account) { return NULL; }

  bool ok = yyjson_mut_obj_add_val(doc, obj, "transaction_valid_start", valid_start);
  ok = ok && yyjson_mut_obj_add_sint(doc, account, "shard_num", hiero->accountID.shardNum);
  ok = ok && yyjson_mut_obj_add_sint(doc, account, "realm_num", hiero->accountID.realmNum);
  ok = ok && yyjson_mut_obj_add_sint(doc, account, "account_num", hiero->accountID.accountNum);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "account_id", account);
  return ok ? obj : NULL;
}

/**
 * @brief The anchor as `{ "type": ..., ... }`, the second member chosen by the type.
 *
 * The union follows the discriminator: a Hiero anchor carries a transaction id, the legacy and
 * node trigger anchors carry a number, and the unspecified anchor carries nothing worth
 * printing -- which is why it is written as the type alone rather than as a zero.
 */
static yyjson_mut_val *ledger_anchor_obj(render_ctx *ctx, const grdw_ledger_anchor *anchor) {
  yyjson_mut_doc *doc = ctx->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  if (!obj) { return NULL; }

  bool ok = yyjson_mut_obj_add_str(doc, obj, "type", grdt_ledger_anchor_to_string(anchor->type));

  switch (anchor->type) {
  case GRDT_LEDGER_ANCHOR_UNSPECIFIED:
    break;
  case GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID: {
    yyjson_mut_val *hiero = hiero_transaction_id_obj(ctx, &anchor->hiero_transaction_id);
    ok = ok && hiero && yyjson_mut_obj_add_val(doc, obj, "hiero_transaction_id", hiero);
    break;
  }
  default:
    ok = ok && yyjson_mut_obj_add_uint(doc, obj, "id", anchor->id);
    break;
  }
  return ok ? obj : NULL;
}

static bool add_account_balances(
    render_ctx *ctx, yyjson_mut_val *root, const grdr_complete_transaction *tx
) {
  yyjson_mut_doc *doc = ctx->doc;
  yyjson_mut_val *array = yyjson_mut_arr(doc);
  if (!array) { return false; }

  for (size_t i = 0; i < tx->account_balances_count; ++i) {
    const grdw_account_balance *balance = &tx->account_balances[i];
    yyjson_mut_val *entry = yyjson_mut_obj(doc);
    yyjson_mut_val *pubkey = hex_val(ctx, balance->pubkey, SIGN_PUBLIC_KEY_SIZE);
    yyjson_mut_val *amount = unit_val(ctx, balance->balance);
    yyjson_mut_val *uuid = uuid_val(ctx, balance->community_uuid);
    if (!entry || !pubkey || !amount || !uuid) { return false; }

    bool ok = yyjson_mut_obj_add_val(doc, entry, "pubkey", pubkey);
    ok = ok && yyjson_mut_obj_add_val(doc, entry, "balance", amount);
    ok = ok && yyjson_mut_obj_add_val(doc, entry, "community_uuid", uuid);
    ok = ok && yyjson_mut_arr_add_val(array, entry);
    if (!ok) { return false; }
  }
  return yyjson_mut_obj_add_val(doc, root, "account_balances", array);
}

static bool add_encrypted_memos(
    render_ctx *ctx, yyjson_mut_val *root, const grdr_complete_transaction *tx
) {
  yyjson_mut_doc *doc = ctx->doc;
  yyjson_mut_val *array = yyjson_mut_arr(doc);
  if (!array) { return false; }

  for (size_t i = 0; i < tx->encrypted_memos_count; ++i) {
    const grdw_encrypted_memo *memo = &tx->encrypted_memos[i];
    yyjson_mut_val *entry = yyjson_mut_obj(doc);
    if (!entry) { return false; }

    bool ok = yyjson_mut_obj_add_str(doc, entry, "type", grdt_memo_key_to_string(memo->type));
    // an empty memo is a memo still: the type is what it carries, and the payload stays out
    // rather than appearing as an empty string that a reader would decode into nothing
    if (memo->memo.size) {
      yyjson_mut_val *payload = hex_val(ctx, memo->memo.data, memo->memo.size);
      ok = ok && payload && yyjson_mut_obj_add_val(doc, entry, "memo", payload);
    }
    ok = ok && yyjson_mut_arr_add_val(array, entry);
    if (!ok) { return false; }
  }
  return yyjson_mut_obj_add_val(doc, root, "encrypted_memos", array);
}

static bool add_signature_pairs(
    render_ctx *ctx, yyjson_mut_val *root, const grdr_complete_transaction *tx
) {
  yyjson_mut_doc *doc = ctx->doc;
  yyjson_mut_val *array = yyjson_mut_arr(doc);
  if (!array) { return false; }

  for (size_t i = 0; i < tx->signature_pairs_count; ++i) {
    const grdw_signature_pair *pair = &tx->signature_pairs[i];
    yyjson_mut_val *entry = yyjson_mut_obj(doc);
    yyjson_mut_val *pubkey = hex_val(ctx, pair->public_key, SIGN_PUBLIC_KEY_SIZE);
    yyjson_mut_val *signature = hex_val(ctx, pair->signature, SIGN_SIGNATURE_SIZE);
    if (!entry || !pubkey || !signature) { return false; }

    bool ok = yyjson_mut_obj_add_val(doc, entry, "public_key", pubkey);
    ok = ok && yyjson_mut_obj_add_val(doc, entry, "signature", signature);
    ok = ok && yyjson_mut_arr_add_val(array, entry);
    if (!ok) { return false; }
  }
  return yyjson_mut_obj_add_val(doc, root, "signature_pairs", array);
}

// ****************** the transaction detail, chosen by the type *****************************
//
// Both unions of grdr_complete_transaction are read through transaction_type, exactly the way
// grdm_complete_transaction_from_wire() writes them -- the switch below is deliberately the
// same shape as the one there, so a type added to one is missing from the other in plain sight.

/**
 * @brief The `transfer` member, shared by the four transaction types that move value.
 *
 * @param[in] with_sender false for a creation, whose sender_pubkey is all zeros by contract.
 *                        Those bytes go out as null: they are the absence of a sender, and
 *                        sixty-four zeros would read as a key that happens to be zero.
 */
static bool add_transfer(
    render_ctx *ctx, yyjson_mut_val *root, const grdr_complete_transaction *tx, bool with_sender
) {
  yyjson_mut_doc *doc = ctx->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *recipient = hex_val(ctx, tx->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE);
  yyjson_mut_val *amount = unit_val(ctx, tx->transfer.amount);
  if (!obj || !recipient || !amount) { return false; }

  bool ok = true;
  if (with_sender) {
    yyjson_mut_val *sender = hex_val(ctx, tx->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE);
    ok = sender && yyjson_mut_obj_add_val(doc, obj, "sender_pubkey", sender);
  } else {
    ok = yyjson_mut_obj_add_null(doc, obj, "sender_pubkey");
  }
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "recipient_pubkey", recipient);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "amount", amount);
  // a creation carries no coin community of its own -- the gdd come into being in the
  // transaction's own community, which the root already names
  if (with_sender) {
    yyjson_mut_val *coin = uuid_val(ctx, tx->transfer.coin_community_uuid);
    ok = ok && coin && yyjson_mut_obj_add_val(doc, obj, "coin_community_uuid", coin);
  }
  return ok && yyjson_mut_obj_add_val(doc, root, "transfer", obj);
}

static bool add_register_address(
    render_ctx *ctx, yyjson_mut_val *root, const grdr_complete_transaction *tx
) {
  yyjson_mut_doc *doc = ctx->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *user = hex_val(ctx, tx->register_address.user_public_key, SIGN_PUBLIC_KEY_SIZE);
  yyjson_mut_val *name_hash = hex_val(ctx, tx->register_address.name_hash, GENERIC_HASH_SIZE);
  yyjson_mut_val *account =
      hex_val(ctx, tx->register_address.account_public_key, SIGN_PUBLIC_KEY_SIZE);
  if (!obj || !user || !name_hash || !account) { return false; }

  bool ok = yyjson_mut_obj_add_val(doc, obj, "user_public_key", user);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "name_hash", name_hash);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "account_public_key", account);
  ok = ok &&
       yyjson_mut_obj_add_str(doc, obj, "address_type", grdt_address_to_string(tx->address_type));
  ok = ok && yyjson_mut_obj_add_uint(doc, obj, "derivation_index", tx->derivation_index);
  return ok && yyjson_mut_obj_add_val(doc, root, "register_address", obj);
}

static bool add_community_root(
    render_ctx *ctx, yyjson_mut_val *root, const grdr_complete_transaction *tx
) {
  yyjson_mut_doc *doc = ctx->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *pubkey = hex_val(ctx, tx->community_root.public_key, SIGN_PUBLIC_KEY_SIZE);
  yyjson_mut_val *gmw = hex_val(ctx, tx->community_root.gmw_public_key, SIGN_PUBLIC_KEY_SIZE);
  yyjson_mut_val *auf = hex_val(ctx, tx->community_root.auf_public_key, SIGN_PUBLIC_KEY_SIZE);
  if (!obj || !pubkey || !gmw || !auf) { return false; }

  bool ok = yyjson_mut_obj_add_val(doc, obj, "public_key", pubkey);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "gmw_public_key", gmw);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "auf_public_key", auf);
  return ok && yyjson_mut_obj_add_val(doc, root, "community_root", obj);
}

/**
 * @brief Both unions in one pass, since one discriminator settles them together.
 *
 * @param[out] ok Set to false when a value could not be built; left alone otherwise, so the
 *                caller separates "this type is unknown" from "this type ran out of memory".
 * @return false when @p tx carries a transaction type this mapping does not describe.
 */
static bool add_transaction_detail(
    render_ctx *ctx, yyjson_mut_val *root, const grdr_complete_transaction *tx, bool *ok
) {
  yyjson_mut_doc *doc = ctx->doc;

  // sorted by expected frequency of occurrence, as in runtime_from_wire.c
  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_TRANSFER:
    *ok = add_transfer(ctx, root, tx, true);
    break;
  case GRDT_TRANSACTION_CREATION:
    *ok = add_transfer(ctx, root, tx, false);
    *ok = *ok && yyjson_mut_obj_add_sint(doc, root, "target_date", tx->target_date);
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    *ok = add_register_address(ctx, root, tx);
    break;
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
    *ok = add_transfer(ctx, root, tx, true);
    *ok = *ok && yyjson_mut_obj_add_sint(doc, root, "timeout_duration", tx->timeout_duration);
    break;
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    *ok = add_transfer(ctx, root, tx, true);
    *ok = *ok && yyjson_mut_obj_add_uint(doc, root, "previous_tx", tx->previous_tx);
    break;
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER:
    // the detail union stays untouched for this type; the previous transaction is all it says
    *ok = yyjson_mut_obj_add_uint(doc, root, "previous_tx", tx->previous_tx);
    break;
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    *ok = add_community_root(ctx, root, tx);
    break;
  default:
    return false;
  }
  return true;
}

// ****************** the whole transaction ***********************************************

static hostmem_result build_root(
    render_ctx *ctx, yyjson_mut_val *root, const grdr_complete_transaction *tx
) {
  yyjson_mut_doc *doc = ctx->doc;

  yyjson_mut_val *created_at = timestamp_val(ctx, &tx->created_at);
  yyjson_mut_val *confirmed_at = timestamp_val(ctx, &tx->confirmed_at);
  yyjson_mut_val *community_uuid = uuid_val(ctx, tx->tx_community_uuid);
  yyjson_mut_val *running_hash = hex_val(ctx, tx->tx_running_hash, GENERIC_HASH_SIZE);
  yyjson_mut_val *anchor = ledger_anchor_obj(ctx, &tx->ledger_anchor);
  if (!created_at || !confirmed_at || !community_uuid || !running_hash || !anchor) {
    return build_failure(ctx);
  }

  bool ok = yyjson_mut_obj_add_uint(doc, root, "tx_nr", tx->tx_nr);
  ok = ok && yyjson_mut_obj_add_str(
                 doc, root, "transaction_type", grdt_transaction_to_string(tx->transaction_type)
             );
  ok = ok && yyjson_mut_obj_add_str(
                 doc, root, "cross_group_type", grdt_cross_group_to_string(tx->cross_group_type)
             );
  ok = ok && yyjson_mut_obj_add_str(
                 doc, root, "balance_derivation_type",
                 grdt_balance_derivation_to_string(tx->balance_derivation_type)
             );
  ok = ok && yyjson_mut_obj_add_val(doc, root, "created_at", created_at);
  ok = ok && yyjson_mut_obj_add_val(doc, root, "confirmed_at", confirmed_at);
  ok = ok && yyjson_mut_obj_add_val(doc, root, "tx_community_uuid", community_uuid);
  ok = ok && yyjson_mut_obj_add_val(doc, root, "ledger_anchor", anchor);
  ok = ok && yyjson_mut_obj_add_val(doc, root, "tx_running_hash", running_hash);
  if (!ok) { return build_failure(ctx); }

  if (!add_transaction_detail(ctx, root, tx, &ok)) { return HOSTMEM_ERROR_ENUM_UNHANDLED; }
  if (!ok) { return build_failure(ctx); }

  // The three arrays are always written, empty ones included: a reader that walks
  // "signature_pairs" finds a list to walk in every transaction rather than having to tell a
  // missing member from an empty one.
  if (!add_account_balances(ctx, root, tx)) { return build_failure(ctx); }
  if (!add_encrypted_memos(ctx, root, tx)) { return build_failure(ctx); }
  if (!add_signature_pairs(ctx, root, tx)) { return build_failure(ctx); }

  // the cross group members are NULL on a local transaction, and stay out of the text there
  if (tx->tx_pairing_community_uuid) {
    yyjson_mut_val *pairing_uuid = uuid_val(ctx, tx->tx_pairing_community_uuid);
    if (!pairing_uuid ||
        !yyjson_mut_obj_add_val(doc, root, "tx_pairing_community_uuid", pairing_uuid)) {
      return build_failure(ctx);
    }
  }
  if (tx->pairing_ledger_anchor) {
    yyjson_mut_val *pairing_anchor = ledger_anchor_obj(ctx, tx->pairing_ledger_anchor);
    if (!pairing_anchor ||
        !yyjson_mut_obj_add_val(doc, root, "pairing_ledger_anchor", pairing_anchor)) {
      return build_failure(ctx);
    }
  }

  if (tx->body_bytes.size) {
    yyjson_mut_val *body = hex_val(ctx, tx->body_bytes.data, tx->body_bytes.size);
    if (!body || !yyjson_mut_obj_add_val(doc, root, "body_bytes", body)) {
      return build_failure(ctx);
    }
  }

  return HOSTMEM_SUCCESS;
}

hostmem_result grdm_complete_transaction_to_json(
    hostmem_memory_block *json,
    const grdr_complete_transaction *tx,
    grdm_json_format format,
    hostmem_multi_arena *work,
    hostmem_multi_arena *result
) {
  if (!json || !tx || !work || !result) { return HOSTMEM_ERROR_NULL_POINTER; }

  // every allocation from here to the finished text is drawn through this
  const yyjson_alc alc = {arena_malloc, arena_realloc, arena_free, work};

  yyjson_mut_doc *doc = yyjson_mut_doc_new(&alc);
  if (!doc) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  yyjson_mut_val *root = yyjson_mut_obj(doc);
  if (!root) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  yyjson_mut_doc_set_root(doc, root);

  render_ctx ctx = {doc, work, HOSTMEM_SUCCESS};
  hostmem_result built = build_root(&ctx, root, tx);
  if (HOSTMEM_SUCCESS != built) { return built; }

  size_t length = 0;
  const yyjson_write_flag flags =
      (GRDM_JSON_PRETTY == format) ? YYJSON_WRITE_PRETTY : YYJSON_WRITE_NOFLAG;
  // written through the same allocator, so the output buffer is scratch like everything else
  char *text = yyjson_mut_write_opts(doc, flags, &alc, &length, NULL);
  if (!text) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  // one more byte for the terminator, and hostmem measures an allocation in uint32_t
  if (length >= UINT32_MAX) { return HOSTMEM_ERROR_ARITHMETIC_OVERFLOW; }

  // The single allocation the result chain ever sees. The text is copied rather than written
  // there directly because the writer grows its buffer as it goes, and every superseded
  // buffer would settle in the chain the caller means to keep.
  uint8_t *out = NULL;
  hostmem_result cloned =
      hostmem_multi_arena_clone(&out, (const uint8_t *)text, (uint32_t)length + 1, result);
  if (HOSTMEM_SUCCESS != cloned) { return cloned; }

  // size counts the characters; the terminator sits after them, the way the rest of the
  // project's _to_string() functions count
  json->data = out;
  json->size = (uint32_t)length;
  return HOSTMEM_SUCCESS;
}
