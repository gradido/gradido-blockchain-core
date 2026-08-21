#include "gradido_blockchain_core/mapping/wire_from_json.h"

#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/unit.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"
#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/data/wire/pb_workspace.h"
#include "gradido_blockchain_core/data/wire/specific_transactions.h"
#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "gradido_blockchain_core/types/address.h"
#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_blockchain_core/types/cross_group.h"
#include "gradido_blockchain_core/types/ledger_anchor.h"
#include "gradido_blockchain_core/types/memo_key.h"
#include "gradido_blockchain_core/types/transaction.h"
#include "hostmem/converter.h"
#include "hostmem/multi_arena.h"

#include "json_arena_alc.c"

#include "yyjson.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**
 * @file
 * @brief Reading the wire structures back out of their JSON.
 *
 * The shape of every function here is the same: a `yyjson_val *` for the member, an out
 * parameter for the field, and `false` for "this member is not what it has to be". The reason
 * goes into the reader's error slot on the way out, so the call sites stay one line each and the
 * caller still learns where it gave way.
 */

// ****************** the reader ************************************************************

/** @brief The document being read, where its memory comes from, and where it failed. */
typedef struct json_reader {
  //! Chain the wire structure's own memory is taken from. Outlives the read.
  hostmem_multi_arena *allocator;
  //! Set once, by the first thing that went wrong. Later failures do not overwrite it.
  grdm_json_error failure;
} json_reader;

/**
 * @brief Record where the read gave way, if nothing has been recorded yet.
 *
 * First writer wins. No path in this file records a second failure today -- a `false` travels
 * outward through short-circuiting `&&` chains and early returns without anyone adding to it --
 * so the guard never fires as the code stands, and a mutation test cannot kill it. It is here
 * for the next reader who wraps one of these in a check of its own: without it, their outer and
 * vaguer name would replace the inner one that actually knows what was wrong.
 *
 * @return Always false, so a call site reads `return fail(reader, "member", "reason");`.
 */
static bool fail(json_reader *reader, const char *member, const char *reason) {
  if (!reader->failure.member && !reader->failure.reason) {
    reader->failure.member = member;
    reader->failure.reason = reason;
  }
  return false;
}

// ****************** fields, read back out of their text ***********************************

/** @brief The member, or NULL when the object does not carry it. */
static yyjson_val *member_of(yyjson_val *obj, const char *key) {
  return yyjson_obj_get(obj, key);
}

/** @brief A required string member, as a null terminated pointer into the document. */
static const char *require_str(
    json_reader *reader, yyjson_val *obj, const char *key, size_t *length
) {
  yyjson_val *val = member_of(obj, key);
  if (!val) {
    fail(reader, key, "member is missing");
    return NULL;
  }
  if (!yyjson_is_str(val)) {
    fail(reader, key, "member is not a string");
    return NULL;
  }
  if (length) { *length = yyjson_get_len(val); }
  return yyjson_get_str(val);
}

/** @brief Fixed length binary, from lowercase hex of exactly twice that many characters. */
static bool require_hex(
    json_reader *reader, yyjson_val *obj, const char *key, uint8_t *dst, uint32_t size
) {
  size_t length = 0;
  const char *hex = require_str(reader, obj, key, &length);
  if (!hex) { return false; }
  if (length != (size_t)size * 2) {
    return fail(reader, key, "hex string is not the length this field takes");
  }
  if (HOSTMEM_SUCCESS != hostmem_binary_from_hex(dst, hex)) {
    return fail(reader, key, "hex string holds a character that is not a hex digit");
  }
  return true;
}

/** @brief Variable length binary, cloned into the reader's allocator. */
static bool require_hex_block(
    json_reader *reader, yyjson_val *obj, const char *key, hostmem_memory_block *dst
) {
  size_t length = 0;
  const char *hex = require_str(reader, obj, key, &length);
  if (!hex) { return false; }
  if (!length || (length & 1u)) {
    return fail(reader, key, "hex string has an odd number of characters");
  }
  if (length / 2 > UINT32_MAX) { return fail(reader, key, "hex string is too long to allocate"); }

  uint32_t size = (uint32_t)(length / 2);
  if (HOSTMEM_SUCCESS != grdw_block_alloc(dst, size, reader->allocator)) {
    return fail(reader, NULL, "the allocator could not open another arena");
  }
  if (HOSTMEM_SUCCESS != hostmem_binary_from_hex(dst->data, hex)) {
    return fail(reader, key, "hex string holds a character that is not a hex digit");
  }
  return true;
}

static bool require_uuid(
    json_reader *reader, yyjson_val *obj, const char *key, uint8_t dst[HOSTMEM_UUID_BINARY_SIZE]
) {
  size_t length = 0;
  const char *text = require_str(reader, obj, key, &length);
  if (!text) { return false; }
  if (length != HOSTMEM_UUID_STRING_LENGTH) {
    return fail(reader, key, "uuid is not in the canonical 8-4-4-4-12 form");
  }
  if (HOSTMEM_SUCCESS != hostmem_uuid_from_string(dst, text)) {
    return fail(reader, key, "uuid holds a character that is not a hex digit, or a misplaced -");
  }
  return true;
}

/**
 * @brief `seconds.nanoseconds` back into the two numbers, the nanoseconds exactly nine digits.
 *
 * Written out rather than handed to strtoll: the format is fixed and narrow, so reading it by
 * hand is both shorter than the checks strtoll would need around it and free of the locale and
 * errno conventions that come with it.
 */
static bool parse_timestamp_text(grdd_timestamp *out, const char *text, size_t length) {
  size_t i = 0;
  bool negative = false;
  if (i < length && text[i] == '-') {
    negative = true;
    ++i;
  }
  if (i >= length || text[i] < '0' || text[i] > '9') { return false; }

  int64_t seconds = 0;
  for (; i < length && text[i] >= '0' && text[i] <= '9'; ++i) {
    int digit = text[i] - '0';
    // the check before the multiply, so the overflow never happens rather than being noticed
    if (seconds > (INT64_MAX - digit) / 10) { return false; }
    seconds = seconds * 10 + digit;
  }
  if (i >= length || text[i] != '.') { return false; }
  ++i;
  // always nine, which is what the writing side pads to; a shorter run would be a different
  // number and a longer one is not a timestamp
  if (length - i != 9) { return false; }

  int32_t nanos = 0;
  for (size_t k = 0; k < 9; ++k) {
    char c = text[i + k];
    if (c < '0' || c > '9') { return false; }
    nanos = nanos * 10 + (c - '0');
  }
  out->seconds = negative ? -seconds : seconds;
  out->nanos = nanos;
  return true;
}

static bool require_timestamp(
    json_reader *reader, yyjson_val *obj, const char *key, grdd_timestamp *dst
) {
  size_t length = 0;
  const char *text = require_str(reader, obj, key, &length);
  if (!text) { return false; }
  if (!parse_timestamp_text(dst, text, length)) {
    return fail(reader, key, "timestamp is not seconds.nanoseconds with nine nanosecond digits");
  }
  return true;
}

static bool require_unit(json_reader *reader, yyjson_val *obj, const char *key, grdd_unit *dst) {
  const char *text = require_str(reader, obj, key, NULL);
  if (!text) { return false; }
  if (!grdd_unit_from_string(dst, text)) {
    return fail(reader, key, "amount is not a decimal number this fixed point type can hold");
  }
  return true;
}

static bool require_uint(json_reader *reader, yyjson_val *obj, const char *key, uint64_t *dst) {
  yyjson_val *val = member_of(obj, key);
  if (!val) { return fail(reader, key, "member is missing"); }
  if (!yyjson_is_uint(val)) { return fail(reader, key, "member is not a positive whole number"); }
  *dst = yyjson_get_uint(val);
  return true;
}

static bool require_sint(json_reader *reader, yyjson_val *obj, const char *key, int64_t *dst) {
  yyjson_val *val = member_of(obj, key);
  if (!val) { return fail(reader, key, "member is missing"); }
  if (!yyjson_is_int(val)) { return fail(reader, key, "member is not a whole number"); }
  *dst = yyjson_get_sint(val);
  return true;
}

// ****************** enums, by the names the writing side prints ***************************
//
// Matched against grdt_*_to_string() rather than against a table of their own. A second table
// would be a second place to add an enumerator, and the day it is forgotten the two directions
// disagree about a name without anything saying so.

static const char *transaction_name(int value) {
  return grdt_transaction_to_string((grdt_transaction)value);
}
static const char *cross_group_name(int value) {
  return grdt_cross_group_to_string((grdt_cross_group)value);
}
static const char *memo_key_name(int value) {
  return grdt_memo_key_to_string((grdt_memo_key)value);
}
static const char *address_name(int value) {
  return grdt_address_to_string((grdt_address)value);
}
static const char *balance_derivation_name(int value) {
  return grdt_balance_derivation_to_string((grdt_balance_derivation)value);
}
static const char *ledger_anchor_name(int value) {
  return grdt_ledger_anchor_to_string((grdt_ledger_anchor)value);
}

/**
 * @brief Walk the enumerators and take the one whose name this is.
 *
 * @p last is inclusive, and gaps in the range are no trouble: the `to_string()` functions answer
 * an unassigned value with their own `..._UNKNOWN`, which no name in a document ever equals.
 */
static bool require_enum(
    json_reader *reader,
    yyjson_val *obj,
    const char *key,
    int *dst,
    int last,
    const char *(*to_string)(int)
) {
  const char *text = require_str(reader, obj, key, NULL);
  if (!text) { return false; }
  for (int value = 0; value <= last; ++value) {
    if (0 == strcmp(text, to_string(value))) {
      *dst = value;
      return true;
    }
  }
  return fail(reader, key, "not the name of any enumerator of this type");
}

// ****************** wire leaves ***********************************************************

static bool read_hiero_transaction_id(
    json_reader *reader, yyjson_val *obj, grdw_hiero_transaction_id *hiero
) {
  if (!require_timestamp(reader, obj, "transaction_valid_start", &hiero->transactionValidStart)) {
    return false;
  }
  yyjson_val *account = member_of(obj, "account_id");
  if (!account || !yyjson_is_obj(account)) {
    return fail(reader, "account_id", "member is missing or is not an object");
  }
  return require_sint(reader, account, "shard_num", &hiero->accountID.shardNum) &&
         require_sint(reader, account, "realm_num", &hiero->accountID.realmNum) &&
         require_sint(reader, account, "account_num", &hiero->accountID.accountNum);
}

/** @brief `{ "type": ..., ... }`, the second member read according to the first. */
static bool read_ledger_anchor(json_reader *reader, yyjson_val *obj, grdw_ledger_anchor *anchor) {
  int type = 0;
  if (!require_enum(
          reader, obj, "type", &type, GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_LINK_ID,
          ledger_anchor_name
      )) {
    return false;
  }
  anchor->type = (grdt_ledger_anchor)type;

  switch (anchor->type) {
  case GRDT_LEDGER_ANCHOR_UNSPECIFIED:
    // the writing side puts nothing beside the type here, and neither is read
    return true;
  case GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID: {
    yyjson_val *hiero = member_of(obj, "hiero_transaction_id");
    if (!hiero || !yyjson_is_obj(hiero)) {
      return fail(reader, "hiero_transaction_id", "member is missing or is not an object");
    }
    return read_hiero_transaction_id(reader, hiero, &anchor->hiero_transaction_id);
  }
  default:
    return require_uint(reader, obj, "id", &anchor->id);
  }
}

/**
 * @brief A required array member, and its length.
 *
 * The arrays are always written, empty ones included, so a missing one is a document that did
 * not come from the writing side rather than an array with nothing in it.
 */
static yyjson_val *require_arr(
    json_reader *reader, yyjson_val *obj, const char *key, size_t *count
) {
  yyjson_val *val = member_of(obj, key);
  if (!val) {
    fail(reader, key, "member is missing");
    return NULL;
  }
  if (!yyjson_is_arr(val)) {
    fail(reader, key, "member is not an array");
    return NULL;
  }
  *count = yyjson_arr_size(val);
  return val;
}

static bool read_account_balances(
    json_reader *reader,
    yyjson_val *obj,
    const char *key,
    grdw_account_balance **balances,
    uint8_t *count
) {
  size_t length = 0;
  yyjson_val *array = require_arr(reader, obj, key, &length);
  if (!array) { return false; }
  if (!length) {
    *balances = NULL;
    *count = 0;
    return true;
  }
  if (length > UINT8_MAX) { return fail(reader, key, "more entries than this field can count"); }

  hostmem_memory_block block = {NULL, 0};
  if (HOSTMEM_SUCCESS !=
      grdw_block_alloc(
          &block, (uint32_t)(length * sizeof(grdw_account_balance)), reader->allocator
      )) {
    return fail(reader, NULL, "the allocator could not open another arena");
  }
  grdw_account_balance *entries = (grdw_account_balance *)block.data;

  size_t index = 0;
  yyjson_val *element = NULL;
  yyjson_arr_iter iter = yyjson_arr_iter_with(array);
  while ((element = yyjson_arr_iter_next(&iter))) {
    if (!yyjson_is_obj(element)) { return fail(reader, key, "an element is not an object"); }
    if (!require_hex(reader, element, "pubkey", entries[index].pubkey, SIGN_PUBLIC_KEY_SIZE) ||
        !require_unit(reader, element, "balance", &entries[index].balance) ||
        !require_uuid(reader, element, "community_uuid", entries[index].community_uuid)) {
      return false;
    }
    ++index;
  }
  *balances = entries;
  *count = (uint8_t)length;
  return true;
}

static bool read_memos(
    json_reader *reader,
    yyjson_val *obj,
    const char *key,
    grdw_encrypted_memo **memos,
    uint8_t *count
) {
  size_t length = 0;
  yyjson_val *array = require_arr(reader, obj, key, &length);
  if (!array) { return false; }
  if (!length) {
    *memos = NULL;
    *count = 0;
    return true;
  }
  if (length > UINT8_MAX) { return fail(reader, key, "more entries than this field can count"); }

  hostmem_memory_block block = {NULL, 0};
  if (HOSTMEM_SUCCESS !=
      grdw_block_alloc(
          &block, (uint32_t)(length * sizeof(grdw_encrypted_memo)), reader->allocator
      )) {
    return fail(reader, NULL, "the allocator could not open another arena");
  }
  grdw_encrypted_memo *entries = (grdw_encrypted_memo *)block.data;

  size_t index = 0;
  yyjson_val *element = NULL;
  yyjson_arr_iter iter = yyjson_arr_iter_with(array);
  while ((element = yyjson_arr_iter_next(&iter))) {
    if (!yyjson_is_obj(element)) { return fail(reader, key, "an element is not an object"); }
    int type = 0;
    if (!require_enum(reader, element, "type", &type, GRDT_MEMO_KEY_PLAIN, memo_key_name)) {
      return false;
    }
    entries[index].type = (grdt_memo_key)type;
    // the payload is left out when there is none, and reading it back the same way is what
    // makes an empty memo survive the round trip as an empty memo
    entries[index].memo.data = NULL;
    entries[index].memo.size = 0;
    if (member_of(element, "memo") &&
        !require_hex_block(reader, element, "memo", &entries[index].memo)) {
      return false;
    }
    ++index;
  }
  *memos = entries;
  *count = (uint8_t)length;
  return true;
}

static bool read_signature_pairs(
    json_reader *reader,
    yyjson_val *obj,
    const char *key,
    grdw_signature_pair **pairs,
    uint8_t *count
) {
  size_t length = 0;
  yyjson_val *array = require_arr(reader, obj, key, &length);
  if (!array) { return false; }
  if (!length) {
    *pairs = NULL;
    *count = 0;
    return true;
  }
  if (length > UINT8_MAX) { return fail(reader, key, "more entries than this field can count"); }

  hostmem_memory_block block = {NULL, 0};
  if (HOSTMEM_SUCCESS !=
      grdw_block_alloc(
          &block, (uint32_t)(length * sizeof(grdw_signature_pair)), reader->allocator
      )) {
    return fail(reader, NULL, "the allocator could not open another arena");
  }
  grdw_signature_pair *entries = (grdw_signature_pair *)block.data;

  size_t index = 0;
  yyjson_val *element = NULL;
  yyjson_arr_iter iter = yyjson_arr_iter_with(array);
  while ((element = yyjson_arr_iter_next(&iter))) {
    if (!yyjson_is_obj(element)) { return fail(reader, key, "an element is not an object"); }
    if (!require_hex(
            reader, element, "public_key", entries[index].public_key, SIGN_PUBLIC_KEY_SIZE
        ) ||
        !require_hex(reader, element, "signature", entries[index].signature, SIGN_SIGNATURE_SIZE)) {
      return false;
    }
    ++index;
  }
  *pairs = entries;
  *count = (uint8_t)length;
  return true;
}

// ****************** the body payloads *****************************************************

static bool read_transfer_amount(
    json_reader *reader, yyjson_val *obj, grdw_transfer_amount *amount
) {
  return require_hex(reader, obj, "pubkey", amount->pubkey, SIGN_PUBLIC_KEY_SIZE) &&
         require_unit(reader, obj, "amount", &amount->amount) &&
         require_uuid(reader, obj, "community_uuid", amount->community_uuid);
}

static bool read_transfer(json_reader *reader, yyjson_val *obj, grdw_gradido_transfer *transfer) {
  yyjson_val *sender = member_of(obj, "sender");
  if (!sender || !yyjson_is_obj(sender)) {
    return fail(reader, "sender", "member is missing or is not an object");
  }
  return read_transfer_amount(reader, sender, &transfer->sender) &&
         require_hex(reader, obj, "recipient", transfer->recipient, SIGN_PUBLIC_KEY_SIZE);
}

/** @brief The payload under the member name the transaction type selects. */
static bool read_body_payload(json_reader *reader, yyjson_val *root, grdw_transaction_body *body) {
  const char *key = NULL;
  switch (body->transaction_type) {
  case GRDT_TRANSACTION_TRANSFER:
    key = "transfer";
    break;
  case GRDT_TRANSACTION_CREATION:
    key = "creation";
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    key = "register_address";
    break;
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
    key = "deferred_transfer";
    break;
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    key = "redeem_deferred_transfer";
    break;
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER:
    key = "timeout_deferred_transfer";
    break;
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    key = "community_root";
    break;
  case GRDT_TRANSACTION_COMMUNITY_FRIENDS_UPDATE:
    key = "community_friends_update";
    break;
  default:
    // GRDT_TRANSACTION_NONE and anything outside the enum name no union member; the enum read
    // above already refused everything outside, so this is NONE
    return fail(reader, "transaction_type", "names no payload, so there is nothing to read");
  }

  yyjson_val *payload = member_of(root, key);
  if (!payload || !yyjson_is_obj(payload)) {
    return fail(reader, key, "the payload this transaction type names is missing");
  }

  switch (body->transaction_type) {
  case GRDT_TRANSACTION_TRANSFER:
    return read_transfer(reader, payload, &body->transfer);
  case GRDT_TRANSACTION_CREATION: {
    yyjson_val *recipient = member_of(payload, "recipient");
    if (!recipient || !yyjson_is_obj(recipient)) {
      return fail(reader, "recipient", "member is missing or is not an object");
    }
    return read_transfer_amount(reader, recipient, &body->creation.recipient) &&
           require_sint(reader, payload, "target_date", &body->creation.target_date.seconds);
  }
  case GRDT_TRANSACTION_REGISTER_ADDRESS: {
    int address_type = 0;
    uint64_t derivation_index = 0;
    if (!require_hex(
            reader, payload, "user_pubkey", body->register_address.user_pubkey, SIGN_PUBLIC_KEY_SIZE
        ) ||
        !require_enum(
            reader, payload, "address_type", &address_type, GRDT_ADDRESS_DEFERRED_TRANSFER,
            address_name
        ) ||
        !require_uint(reader, payload, "derivation_index", &derivation_index) ||
        !require_hex(
            reader, payload, "name_hash", body->register_address.name_hash, GENERIC_HASH_SIZE
        ) ||
        !require_hex(
            reader, payload, "account_pubkey", body->register_address.account_pubkey,
            SIGN_PUBLIC_KEY_SIZE
        )) {
      return false;
    }
    if (derivation_index > UINT32_MAX) {
      return fail(reader, "derivation_index", "larger than this field can hold");
    }
    body->register_address.address_type = (grdt_address)address_type;
    body->register_address.derivation_index = (uint32_t)derivation_index;
    return true;
  }
  case GRDT_TRANSACTION_DEFERRED_TRANSFER: {
    yyjson_val *transfer = member_of(payload, "transfer");
    uint64_t timeout = 0;
    if (!transfer || !yyjson_is_obj(transfer)) {
      return fail(reader, "transfer", "member is missing or is not an object");
    }
    if (!read_transfer(reader, transfer, &body->deferred_transfer.transfer) ||
        !require_uint(reader, payload, "timeout_duration", &timeout)) {
      return false;
    }
    if (timeout > UINT32_MAX) {
      return fail(reader, "timeout_duration", "larger than this field can hold");
    }
    body->deferred_transfer.timeout_duration = (uint32_t)timeout;
    return true;
  }
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER: {
    yyjson_val *transfer = member_of(payload, "transfer");
    if (!transfer || !yyjson_is_obj(transfer)) {
      return fail(reader, "transfer", "member is missing or is not an object");
    }
    return require_uint(
               reader, payload, "deferred_transfer_transaction_nr",
               &body->redeem_deferred_transfer.deferred_transfer_transaction_nr
           ) &&
           read_transfer(reader, transfer, &body->redeem_deferred_transfer.transfer);
  }
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER:
    return require_uint(
        reader, payload, "deferred_transfer_transaction_nr",
        &body->timeout_deferred_transfer.deferred_transfer_transaction_nr
    );
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    return require_hex(
               reader, payload, "pubkey", body->community_root.pubkey, SIGN_PUBLIC_KEY_SIZE
           ) &&
           require_hex(
               reader, payload, "gmw_pubkey", body->community_root.gmw_pubkey, SIGN_PUBLIC_KEY_SIZE
           ) &&
           require_hex(
               reader, payload, "auf_pubkey", body->community_root.auf_pubkey, SIGN_PUBLIC_KEY_SIZE
           );
  case GRDT_TRANSACTION_COMMUNITY_FRIENDS_UPDATE: {
    yyjson_val *fusion = member_of(payload, "color_fusion");
    if (!fusion || !yyjson_is_bool(fusion)) {
      return fail(reader, "color_fusion", "member is missing or is not a boolean");
    }
    body->community_friends_update.color_fusion = yyjson_get_bool(fusion);
    return true;
  }
  default:
    return fail(reader, "transaction_type", "names no payload, so there is nothing to read");
  }
}

// ****************** the three structures **************************************************

static bool read_body(json_reader *reader, yyjson_val *root, grdw_transaction_body *body) {
  int transaction_type = 0;
  int cross_group = 0;
  if (!require_enum(
          reader, root, "transaction_type", &transaction_type, GRDT_TRANSACTION_COUNT - 1,
          transaction_name
      ) ||
      !require_enum(
          reader, root, "cross_group_type", &cross_group, GRDT_CROSS_GROUP_CROSS, cross_group_name
      ) ||
      !require_timestamp(reader, root, "created_at", &body->created_at)) {
    return false;
  }
  body->transaction_type = (grdt_transaction)transaction_type;
  body->type = (grdt_cross_group)cross_group;

  // written only by a body that carries one, and read the same way
  if (member_of(root, "other_community_uuid")) {
    hostmem_memory_block uuid = {NULL, 0};
    if (HOSTMEM_SUCCESS != grdw_block_alloc(&uuid, HOSTMEM_UUID_BINARY_SIZE, reader->allocator)) {
      return fail(reader, NULL, "the allocator could not open another arena");
    }
    if (!require_uuid(reader, root, "other_community_uuid", uuid.data)) { return false; }
    body->other_community_uuid = uuid.data;
  }

  return read_body_payload(reader, root, body) &&
         read_memos(reader, root, "memos", &body->memos, &body->memos_count);
}

static bool read_gradido_transaction(
    json_reader *reader, yyjson_val *root, grdw_gradido_transaction *tx
) {
  if (!read_signature_pairs(reader, root, "sig_map", &tx->sig_map, &tx->sig_map_count)) {
    return false;
  }

  // the decoded body is not a way back to the bytes the signatures are over -- see the module
  // text. A document that carries only that is refused rather than half read.
  if (!member_of(root, "body_bytes") && member_of(root, "body")) {
    return fail(
        reader, "body_bytes",
        "the decoded body cannot stand in for the exact bytes the signatures are over"
    );
  }
  if (member_of(root, "body_bytes") &&
      !require_hex_block(reader, root, "body_bytes", &tx->body_bytes)) {
    return false;
  }

  // absent on a local transaction, and left unspecified here the same way
  if (member_of(root, "pairing_ledger_anchor")) {
    yyjson_val *anchor = member_of(root, "pairing_ledger_anchor");
    if (!yyjson_is_obj(anchor)) {
      return fail(reader, "pairing_ledger_anchor", "member is not an object");
    }
    if (!read_ledger_anchor(reader, anchor, &tx->pairing_ledger_anchor)) { return false; }
  }
  return true;
}

static bool read_confirmed_transaction(
    json_reader *reader, yyjson_val *root, grdw_confirmed_transaction *tx
) {
  int balance_derivation = 0;
  yyjson_val *nested = member_of(root, "transaction");
  yyjson_val *anchor = member_of(root, "ledger_anchor");
  if (!nested || !yyjson_is_obj(nested)) {
    return fail(reader, "transaction", "member is missing or is not an object");
  }
  if (!anchor || !yyjson_is_obj(anchor)) {
    return fail(reader, "ledger_anchor", "member is missing or is not an object");
  }

  return require_uint(reader, root, "id", &tx->id) &&
         read_gradido_transaction(reader, nested, &tx->transaction) &&
         require_timestamp(reader, root, "confirmed_at", &tx->confirmed_at) &&
         require_hex(reader, root, "running_hash", tx->running_hash, GENERIC_HASH_SIZE) &&
         read_ledger_anchor(reader, anchor, &tx->ledger_anchor) &&
         require_enum(
             reader, root, "balance_derivation", &balance_derivation,
             GRDT_BALANCE_DERIVATION_EXTERN, balance_derivation_name
         ) &&
         (tx->balance_derivation = (grdt_balance_derivation)balance_derivation, true) &&
         read_account_balances(
             reader, root, "account_balances", &tx->account_balances, &tx->account_balances_count
         );
}

// ****************** the course ************************************************************

/**
 * @brief Parse the text into a document on the work chain and hand back its root object.
 *
 * yyjson draws from the same chained allocator the writing side uses, so a read costs the host
 * nothing beyond what the caller's chain already holds. The document is left in @p work on
 * return: the strings the wire structure keeps are copied out of it before this returns, so the
 * caller is free to reset the chain the moment the call is over.
 */
static hostmem_result open_document(
    yyjson_doc **doc,
    yyjson_val **root,
    const char *json,
    size_t json_size,
    grdm_json_error *error,
    hostmem_multi_arena *work
) {
  const yyjson_alc alc = grdm_json_arena_alc(work);
  yyjson_read_err read_error;
  // the cast is safe without YYJSON_READ_INSITU: that flag is the only thing that makes yyjson
  // write into the input, and it is not passed. The header promises the text is not modified.
  *doc = yyjson_read_opts((char *)json, json_size, 0, &alc, &read_error);
  if (!*doc) {
    if (error) {
      error->member = NULL;
      // yyjson's own message, a string literal from its table rather than anything allocated
      error->reason = read_error.msg;
    }
    return HOSTMEM_ERROR_DECODE_FAILED;
  }
  *root = yyjson_doc_get_root(*doc);
  if (!*root || !yyjson_is_obj(*root)) {
    if (error) {
      error->member = NULL;
      error->reason = "the document's root is not an object";
    }
    return HOSTMEM_ERROR_DECODE_FAILED;
  }
  return HOSTMEM_SUCCESS;
}

hostmem_result grdm_transaction_body_from_json(
    grdw_transaction_body *body,
    const char *json,
    size_t json_size,
    grdm_json_error *error,
    hostmem_multi_arena *work,
    hostmem_multi_arena *allocator
) {
  if (!body || !json || !work || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!json_size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  yyjson_doc *doc = NULL;
  yyjson_val *root = NULL;
  hostmem_result opened = open_document(&doc, &root, json, json_size, error, work);
  if (HOSTMEM_SUCCESS != opened) { return opened; }

  grdw_transaction_body_init(body);
  json_reader reader = {allocator, {NULL, NULL}};
  if (!read_body(&reader, root, body)) {
    grdw_transaction_body_init(body);
    if (error) { *error = reader.failure; }
    return HOSTMEM_ERROR_DECODE_FAILED;
  }
  return HOSTMEM_SUCCESS;
}

hostmem_result grdm_gradido_transaction_from_json(
    grdw_gradido_transaction *tx,
    const char *json,
    size_t json_size,
    grdm_json_error *error,
    hostmem_multi_arena *work,
    hostmem_multi_arena *allocator
) {
  if (!tx || !json || !work || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!json_size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  yyjson_doc *doc = NULL;
  yyjson_val *root = NULL;
  hostmem_result opened = open_document(&doc, &root, json, json_size, error, work);
  if (HOSTMEM_SUCCESS != opened) { return opened; }

  grdw_gradido_transaction_init(tx);
  json_reader reader = {allocator, {NULL, NULL}};
  if (!read_gradido_transaction(&reader, root, tx)) {
    grdw_gradido_transaction_init(tx);
    if (error) { *error = reader.failure; }
    return HOSTMEM_ERROR_DECODE_FAILED;
  }
  return HOSTMEM_SUCCESS;
}

hostmem_result grdm_confirmed_transaction_from_json(
    grdw_confirmed_transaction *tx,
    const char *json,
    size_t json_size,
    grdm_json_error *error,
    hostmem_multi_arena *work,
    hostmem_multi_arena *allocator
) {
  if (!tx || !json || !work || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!json_size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  yyjson_doc *doc = NULL;
  yyjson_val *root = NULL;
  hostmem_result opened = open_document(&doc, &root, json, json_size, error, work);
  if (HOSTMEM_SUCCESS != opened) { return opened; }

  grdw_confirmed_transaction_init(tx);
  json_reader reader = {allocator, {NULL, NULL}};
  if (!read_confirmed_transaction(&reader, root, tx)) {
    grdw_confirmed_transaction_init(tx);
    if (error) { *error = reader.failure; }
    return HOSTMEM_ERROR_DECODE_FAILED;
  }
  return HOSTMEM_SUCCESS;
}
