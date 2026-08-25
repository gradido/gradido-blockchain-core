#include "gradido_blockchain_core/mapping/runtime_from_json.h"

#include "arnm/arena.h"
#include "arnm/converter.h"
#include "arnm/memory.h"
#include "arnm/memory_block.h"
#include "complete_transaction_json.h"
#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/data/timestamp.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/hiero.h"
#include "gradido_blockchain_core/data/wire/ledger_anchor.h"
#include "gradido_blockchain_core/types/address.h"
#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_blockchain_core/types/cross_group.h"
#include "gradido_blockchain_core/types/ledger_anchor.h"
#include "gradido_blockchain_core/types/memo_key.h"
#include "gradido_blockchain_core/types/transaction.h"

#include <stdint.h>
#include <string.h>

// ********** enumerations, read back through the names they were written by ****************

/*
 * Every enumeration in this project already has a to_string() that answers its enumerator's own
 * spelling, and these six walk that answer backwards: a candidate value is named and compared,
 * and the first name that matches is the value. No second table is written, so there is no
 * second table to fall out of step with the first -- a value added to an enum is readable here
 * the moment its name is added there.
 *
 * The scan is linear over a handful of names and runs once per document per field. Where that
 * ever stops being free, the fix is a sorted table generated from the same source, not a copy
 * maintained by hand.
 */

/** @brief Value of the transaction type @p name spells. @retval ARNM_ERROR_ENUM_UNKNOWN No such
 *         enumerator. */
static arnm_result transaction_from_string(grdt_transaction *out, const char *name) {
  for (int value = 0; value < (int)GRDT_TRANSACTION_COUNT; ++value) {
    if (0 == strcmp(grdt_transaction_to_string((grdt_transaction)value), name)) {
      *out = (grdt_transaction)value;
      return ARNM_SUCCESS;
    }
  }
  return ARNM_ERROR_ENUM_UNKNOWN;
}

/** @brief Value of the balance derivation @p name spells. */
static arnm_result balance_derivation_from_string(grdt_balance_derivation *out, const char *name) {
  for (int value = 0; value <= (int)GRDT_BALANCE_DERIVATION_EXTERN; ++value) {
    if (0 == strcmp(grdt_balance_derivation_to_string((grdt_balance_derivation)value), name)) {
      *out = (grdt_balance_derivation)value;
      return ARNM_SUCCESS;
    }
  }
  return ARNM_ERROR_ENUM_UNKNOWN;
}

/** @brief Value of the cross group type @p name spells. */
static arnm_result cross_group_from_string(grdt_cross_group *out, const char *name) {
  for (int value = 0; value <= (int)GRDT_CROSS_GROUP_CROSS; ++value) {
    if (0 == strcmp(grdt_cross_group_to_string((grdt_cross_group)value), name)) {
      *out = (grdt_cross_group)value;
      return ARNM_SUCCESS;
    }
  }
  return ARNM_ERROR_ENUM_UNKNOWN;
}

/** @brief Value of the address type @p name spells. */
static arnm_result address_from_string(grdt_address *out, const char *name) {
  for (int value = 0; value <= (int)GRDT_ADDRESS_DEFERRED_TRANSFER; ++value) {
    if (0 == strcmp(grdt_address_to_string((grdt_address)value), name)) {
      *out = (grdt_address)value;
      return ARNM_SUCCESS;
    }
  }
  return ARNM_ERROR_ENUM_UNKNOWN;
}

/** @brief Value of the memo key type @p name spells. */
static arnm_result memo_key_from_string(grdt_memo_key *out, const char *name) {
  for (int value = 0; value <= (int)GRDT_MEMO_KEY_PLAIN; ++value) {
    if (0 == strcmp(grdt_memo_key_to_string((grdt_memo_key)value), name)) {
      *out = (grdt_memo_key)value;
      return ARNM_SUCCESS;
    }
  }
  return ARNM_ERROR_ENUM_UNKNOWN;
}

/**
 * @brief Value of the ledger anchor type @p name spells.
 *
 * The one enumeration here with a gap in it: 1 belongs to no enumerator, and its slot answers
 * the unknown name. That name is skipped rather than matched, so a document literally carrying
 * it is refused instead of quietly becoming the value nobody named.
 */
static arnm_result ledger_anchor_from_string(grdt_ledger_anchor *out, const char *name) {
  for (int value = 0; value <= (int)GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_LINK_ID;
       ++value) {
    const char *candidate = grdt_ledger_anchor_to_string((grdt_ledger_anchor)value);
    if (0 == strcmp(candidate, "GRDT_LEDGER_ANCHOR_UNKNOWN")) { continue; }
    if (0 == strcmp(candidate, name)) {
      *out = (grdt_ledger_anchor)value;
      return ARNM_SUCCESS;
    }
  }
  return ARNM_ERROR_ENUM_UNKNOWN;
}

// ********** the small readings every field is built from **********************************

/*
 * A getter answering NULL has already told the reader why -- the member was missing, or it was
 * of another type -- and the reader keeps that first refusal and answers it at the end. So the
 * helpers below hand back ARNM_SUCCESS on a NULL and leave the field cleared: the document's
 * verdict is asked for once, where it belongs, and nothing uninitialised travels on in the
 * meantime. What they do refuse themselves is a string that is there and is wrong, because a
 * length that does not match its field is the one mistake no later check would catch.
 */

/**
 * @brief Read a hex member into a field whose length is fixed.
 *
 * @param[in,out] reader Reader positioned on the object holding @p key.
 * @param[in]     key    Member to read.
 * @param[out]    out    @p size bytes; cleared where the member could not be read.
 * @param[in]     size   Bytes the field holds; the string has to be exactly twice as long.
 * @retval ARNM_SUCCESS             The bytes are in @p out, or the reader recorded why not.
 * @retval ARNM_ERROR_DECODE_FAILED The string is there and is not @p size bytes of hex.
 */
static arnm_result read_hex_fixed(
    arnm_json_reader *reader, const char *key, uint8_t *out, uint32_t size
) {
  uint32_t length = 0;
  const char *hex = arnm_json_reader_get_string_length(reader, key, &length);
  if (!hex) {
    memset(out, 0, size);
    return ARNM_SUCCESS;
  }
  if (length != size * 2u) { return ARNM_ERROR_DECODE_FAILED; }
  return arnm_binary_from_hex(out, hex);
}

/**
 * @brief Read a uuid member in the canonical 8-4-4-4-12 form into 16 bytes.
 *
 * @param[in,out] reader Reader positioned on the object holding @p key.
 * @param[in]     key    Member to read.
 * @param[out]    out    @ref ARNM_UUID_BINARY_SIZE bytes; cleared where it could not be read.
 * @retval ARNM_SUCCESS             The bytes are in @p out, or the reader recorded why not.
 * @retval ARNM_ERROR_DECODE_FAILED The string is there and is no uuid.
 */
static arnm_result read_uuid(arnm_json_reader *reader, const char *key, uint8_t *out) {
  uint32_t length = 0;
  const char *text = arnm_json_reader_get_string_length(reader, key, &length);
  if (!text) {
    memset(out, 0, ARNM_UUID_BINARY_SIZE);
    return ARNM_SUCCESS;
  }
  if (ARNM_UUID_STRING_LENGTH != length) { return ARNM_ERROR_DECODE_FAILED; }
  return arnm_uuid_from_string(out, text);
}

/**
 * @brief Read a hex member of any length into a block drawn from @p memory.
 *
 * @p out is cleared first, so an absent member and an empty string both leave the empty block
 * the writer would have produced them from. Only bytes that are really there cost an
 * allocation.
 *
 * @param[out]    out    Block to fill; not NULL. Written in full, read not at all.
 * @param[in,out] reader Reader positioned on the object holding @p key.
 * @param[in]     key    Member to read.
 * @param[in,out] memory Where the bytes come from -- the transaction's own arena.
 * @retval ARNM_SUCCESS             The bytes are in @p out, or there were none to take.
 * @retval ARNM_ERROR_DECODE_FAILED The string is there and is not an even run of hex digits.
 * @retval Anything arnm_memory_block_alloc() can return.
 */
static arnm_result read_hex_block(
    arnm_memory_block *out, arnm_json_reader *reader, const char *key, arnm *memory
) {
  out->data = NULL;
  out->size = 0;

  uint32_t length = 0;
  const char *hex = arnm_json_reader_get_string_length(reader, key, &length);
  if (!hex) { return ARNM_SUCCESS; }
  if (length % 2u) { return ARNM_ERROR_DECODE_FAILED; }
  if (!length) { return ARNM_SUCCESS; }

  const arnm_result result = arnm_memory_block_alloc(out, length / 2u, memory);
  if (ARNM_SUCCESS != result) { return result; }
  return arnm_binary_from_hex(out->data, hex);
}

/**
 * @brief Read a string member and turn it into the enumerator it spells.
 *
 * @param[in,out] reader Reader positioned on the object holding @p key.
 * @param[in]     key    Member to read.
 * @return The member's text, or NULL where the reader could not read one -- in which case it
 *         has recorded why and the caller leaves the field at zero.
 */
static const char *read_enum_name(arnm_json_reader *reader, const char *key) {
  return arnm_json_reader_get_string(reader, key);
}

/**
 * @brief Read a timestamp member: whole seconds, and the nanos within the second.
 *
 * Both are left at zero where the member is missing; the reader keeps the reason.
 */
static void read_timestamp(grdd_timestamp *out, arnm_json_reader *reader, const char *key) {
  arnm_json_value *outer = arnm_json_reader_enter(reader, key);
  out->seconds = arnm_json_reader_get_int64(reader, GRDM_JSON_KEY_SECONDS);
  out->nanos = arnm_json_reader_get_int32(reader, GRDM_JSON_KEY_NANOS);
  arnm_json_reader_leave(reader, outer);
}

/**
 * @brief Read a ledger anchor the reader is already standing on.
 *
 * The type decides which member of the union is there to be read, exactly as it decided which
 * one was written. Every branch writes the whole union, so no byte of it is left as the arena
 * handed it over.
 *
 * @param[out]    out    Anchor to fill; not NULL.
 * @param[in,out] reader Reader whose current value is the anchor's object.
 * @retval ARNM_SUCCESS           The anchor is in @p out.
 * @retval ARNM_ERROR_ENUM_UNKNOWN The type member spells no anchor this library has.
 */
static arnm_result read_ledger_anchor(grdw_ledger_anchor *out, arnm_json_reader *reader) {
  out->type = GRDT_LEDGER_ANCHOR_UNSPECIFIED;
  out->id = 0;

  const char *type_name = read_enum_name(reader, GRDM_JSON_KEY_TYPE);
  if (!type_name) { return ARNM_SUCCESS; }
  const arnm_result result = ledger_anchor_from_string(&out->type, type_name);
  if (ARNM_SUCCESS != result) { return result; }

  switch (out->type) {
  case GRDT_LEDGER_ANCHOR_UNSPECIFIED:
    break;
  case GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID: {
    grdw_hiero_transaction_id *hiero = &out->hiero_transaction_id;
    arnm_json_value *outer = arnm_json_reader_enter(reader, GRDM_JSON_KEY_HIERO_TRANSACTION_ID);
    read_timestamp(&hiero->transactionValidStart, reader, GRDM_JSON_KEY_TRANSACTION_VALID_START);
    arnm_json_value *account = arnm_json_reader_enter(reader, GRDM_JSON_KEY_ACCOUNT_ID);
    hiero->accountID.shardNum = arnm_json_reader_get_int64(reader, GRDM_JSON_KEY_SHARD_NUM);
    hiero->accountID.realmNum = arnm_json_reader_get_int64(reader, GRDM_JSON_KEY_REALM_NUM);
    hiero->accountID.accountNum = arnm_json_reader_get_int64(reader, GRDM_JSON_KEY_ACCOUNT_NUM);
    arnm_json_reader_leave(reader, account);
    arnm_json_reader_leave(reader, outer);
    break;
  }
  default:
    out->id = arnm_json_reader_get_uint64(reader, GRDM_JSON_KEY_ID);
    break;
  }
  return ARNM_SUCCESS;
}

// ********** sizing the transaction's own arena ********************************************

/**
 * @brief Bytes the arrays and the byte blocks of this document will occupy.
 *
 * The counterpart of calculate_memory_size() in the wire mapping, and it answers the same
 * question from the other bank: how much ground the transaction needs before a byte of it is
 * copied. Only counts are looked at and only lengths are measured -- nothing is read, which is
 * why this pass reaches for @c has(), @c count() and @c type_of() alone. Those three record
 * nothing, and a refusal recorded here would make every later reading answer empty and turn
 * the fill pass below into a run of zeros.
 *
 * @param[out]    out    Bytes to open the arena with, aligned the way an arena charges.
 * @param[in,out] reader Reader whose current value is the document's root object.
 * @retval ARNM_SUCCESS                    @p out holds the figure.
 * @retval ARNM_ERROR_DECODE_FAILED        A memo or @c body_bytes is an odd run of hex digits.
 * @retval ARNM_ERROR_RESOURCE_SIZE_EXCEED The sum is past what an arena can be opened with.
 */
static arnm_result calculate_memory_size(uint32_t *out, arnm_json_reader *reader) {
  uint64_t total = 0;
  arnm_result result = ARNM_SUCCESS;

  if (arnm_json_reader_has(reader, GRDM_JSON_KEY_ACCOUNT_BALANCES)) {
    arnm_json_value *array = arnm_json_reader_enter(reader, GRDM_JSON_KEY_ACCOUNT_BALANCES);
    total += ARNM_ALIGN8((uint64_t)arnm_json_reader_count(reader) * sizeof(grdw_account_balance));
    arnm_json_reader_leave(reader, array);
  }

  if (arnm_json_reader_has(reader, GRDM_JSON_KEY_ENCRYPTED_MEMOS)) {
    arnm_json_value *array = arnm_json_reader_enter(reader, GRDM_JSON_KEY_ENCRYPTED_MEMOS);
    const uint32_t count = arnm_json_reader_count(reader);
    total += ARNM_ALIGN8((uint64_t)count * sizeof(grdw_encrypted_memo));
    for (uint32_t i = 0; i < count && ARNM_SUCCESS == result; ++i) {
      arnm_json_value *element = arnm_json_reader_enter_at(reader, i);
      if (ARNM_JSON_TYPE_STRING == arnm_json_reader_type_of(reader, GRDM_JSON_KEY_MEMO)) {
        uint32_t length = 0;
        arnm_json_reader_get_string_length(reader, GRDM_JSON_KEY_MEMO, &length);
        if (length % 2u) {
          result = ARNM_ERROR_DECODE_FAILED;
        } else {
          total += ARNM_ALIGN8((uint64_t)(length / 2u));
        }
      }
      arnm_json_reader_leave(reader, element);
    }
    arnm_json_reader_leave(reader, array);
  }

  if (arnm_json_reader_has(reader, GRDM_JSON_KEY_SIGNATURE_PAIRS)) {
    arnm_json_value *array = arnm_json_reader_enter(reader, GRDM_JSON_KEY_SIGNATURE_PAIRS);
    total += ARNM_ALIGN8((uint64_t)arnm_json_reader_count(reader) * sizeof(grdw_signature_pair));
    arnm_json_reader_leave(reader, array);
  }

  if (arnm_json_reader_has(reader, GRDM_JSON_KEY_TX_PAIRING_COMMUNITY_UUID)) {
    total += ARNM_ALIGN8((uint64_t)ARNM_UUID_BINARY_SIZE);
  }
  if (arnm_json_reader_has(reader, GRDM_JSON_KEY_PAIRING_LEDGER_ANCHOR)) {
    total += ARNM_ALIGN8((uint64_t)sizeof(grdw_ledger_anchor));
  }

  if (ARNM_JSON_TYPE_STRING == arnm_json_reader_type_of(reader, GRDM_JSON_KEY_BODY_BYTES)) {
    uint32_t length = 0;
    arnm_json_reader_get_string_length(reader, GRDM_JSON_KEY_BODY_BYTES, &length);
    if (length % 2u) {
      result = ARNM_ERROR_DECODE_FAILED;
    } else {
      total += ARNM_ALIGN8((uint64_t)(length / 2u));
    }
  }

  if (ARNM_SUCCESS != result) { return result; }
  // the counts and the lengths came out of a document, and a document may claim more than a
  // uint32_t holds. Refusing here is what keeps a sum past 4 GiB from wrapping into a small
  // arena that every later allocation then runs past -- the same guard the wire mapping keeps.
  if (total > ARNM_MAX_ALLOC_SIZE) { return ARNM_ERROR_RESOURCE_SIZE_EXCEED; }
  *out = (uint32_t)total;
  return ARNM_SUCCESS;
}

// ********** the transaction, branch by branch *********************************************

/** @brief Read the transfer branch, which serves a creation and both deferred transfers too. */
static arnm_result read_transfer(grdr_complete_transaction *tx, arnm_json_reader *reader) {
  arnm_json_value *outer = arnm_json_reader_enter(reader, GRDM_JSON_KEY_TRANSFER);
  arnm_result result = read_hex_fixed(
      reader, GRDM_JSON_KEY_SENDER_PUBKEY, tx->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS == result) {
    result = read_hex_fixed(
        reader, GRDM_JSON_KEY_RECIPIENT_PUBKEY, tx->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE
    );
  }
  if (ARNM_SUCCESS == result) {
    tx->transfer.amount = arnm_json_reader_get_int64(reader, GRDM_JSON_KEY_AMOUNT);
    result = read_uuid(reader, GRDM_JSON_KEY_COIN_COMMUNITY_UUID, tx->transfer.coin_community_uuid);
  }
  arnm_json_reader_leave(reader, outer);
  return result;
}

/** @brief Read the register-address branch, address type and derivation index included. */
static arnm_result read_register_address(grdr_complete_transaction *tx, arnm_json_reader *reader) {
  arnm_json_value *outer = arnm_json_reader_enter(reader, GRDM_JSON_KEY_REGISTER_ADDRESS);
  arnm_result result = read_hex_fixed(
      reader, GRDM_JSON_KEY_USER_PUBLIC_KEY, tx->register_address.user_public_key,
      SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS == result) {
    result = read_hex_fixed(
        reader, GRDM_JSON_KEY_NAME_HASH, tx->register_address.name_hash, GENERIC_HASH_SIZE
    );
  }
  if (ARNM_SUCCESS == result) {
    result = read_hex_fixed(
        reader, GRDM_JSON_KEY_ACCOUNT_PUBLIC_KEY, tx->register_address.account_public_key,
        SIGN_PUBLIC_KEY_SIZE
    );
  }
  if (ARNM_SUCCESS == result) {
    // the second union, not the one above: same transaction, other half of the struct
    const char *address_name = read_enum_name(reader, GRDM_JSON_KEY_ADDRESS_TYPE);
    tx->address_type = GRDT_ADDRESS_NONE;
    if (address_name) { result = address_from_string(&tx->address_type, address_name); }
    tx->derivation_index = arnm_json_reader_get_uint32(reader, GRDM_JSON_KEY_DERIVATION_INDEX);
  }
  arnm_json_reader_leave(reader, outer);
  return result;
}

/** @brief Read the community-root branch: the community key and its two account keys. */
static arnm_result read_community_root(grdr_complete_transaction *tx, arnm_json_reader *reader) {
  arnm_json_value *outer = arnm_json_reader_enter(reader, GRDM_JSON_KEY_COMMUNITY_ROOT);
  arnm_result result = read_hex_fixed(
      reader, GRDM_JSON_KEY_PUBLIC_KEY, tx->community_root.public_key, SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS == result) {
    result = read_hex_fixed(
        reader, GRDM_JSON_KEY_GMW_PUBLIC_KEY, tx->community_root.gmw_public_key,
        SIGN_PUBLIC_KEY_SIZE
    );
  }
  if (ARNM_SUCCESS == result) {
    result = read_hex_fixed(
        reader, GRDM_JSON_KEY_AUF_PUBLIC_KEY, tx->community_root.auf_public_key,
        SIGN_PUBLIC_KEY_SIZE
    );
  }
  arnm_json_reader_leave(reader, outer);
  return result;
}

/**
 * @brief Read the detail and the context the transaction's type owns, and nothing else.
 *
 * Sorted by expected frequency of occurrence, the same order the wire mapping keeps. A type
 * with no layout here is refused rather than half read -- the same refusal
 * grdm_complete_transaction_from_wire() answers for the same types.
 */
static arnm_result read_transaction_detail(
    grdr_complete_transaction *tx, arnm_json_reader *reader
) {
  arnm_result result = ARNM_SUCCESS;
  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_TRANSFER:
    result = read_transfer(tx, reader);
    break;
  case GRDT_TRANSACTION_CREATION:
    result = read_transfer(tx, reader);
    if (ARNM_SUCCESS != result) { break; }
    tx->target_date = arnm_json_reader_get_int64(reader, GRDM_JSON_KEY_TARGET_DATE);
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    result = read_register_address(tx, reader);
    break;
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
    result = read_transfer(tx, reader);
    if (ARNM_SUCCESS != result) { break; }
    tx->timeout_duration = arnm_json_reader_get_int64(reader, GRDM_JSON_KEY_TIMEOUT_DURATION);
    break;
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    result = read_transfer(tx, reader);
    if (ARNM_SUCCESS != result) { break; }
    tx->previous_tx = arnm_json_reader_get_uint64(reader, GRDM_JSON_KEY_PREVIOUS_TX);
    break;
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER:
    tx->previous_tx = arnm_json_reader_get_uint64(reader, GRDM_JSON_KEY_PREVIOUS_TX);
    break;
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    result = read_community_root(tx, reader);
    break;
  default:
    result = ARNM_ERROR_ENUM_UNHANDLED;
    break;
  }
  return result;
}

/** @brief Read the balances as they settled, the memos, and the signatures over them. */
static arnm_result read_arrays(grdr_complete_transaction *tx, arnm_json_reader *reader) {
  arnm_result result = ARNM_SUCCESS;

  if (arnm_json_reader_has(reader, GRDM_JSON_KEY_ACCOUNT_BALANCES)) {
    arnm_json_value *array = arnm_json_reader_enter(reader, GRDM_JSON_KEY_ACCOUNT_BALANCES);
    const uint32_t count = arnm_json_reader_count(reader);
    if (count) {
      // the cast is what calculate_memory_size() already bounded: the arena could not have been
      // opened if this product did not fit
      result = arnm_alloc(
          (uint8_t **)&tx->account_balances, (uint32_t)(count * sizeof(grdw_account_balance)),
          &tx->memory_area
      );
      for (uint32_t i = 0; i < count && ARNM_SUCCESS == result; ++i) {
        grdw_account_balance *balance = &tx->account_balances[i];
        arnm_json_value *element = arnm_json_reader_enter_at(reader, i);
        result =
            read_hex_fixed(reader, GRDM_JSON_KEY_PUBKEY, balance->pubkey, SIGN_PUBLIC_KEY_SIZE);
        if (ARNM_SUCCESS == result) {
          balance->balance = arnm_json_reader_get_int64(reader, GRDM_JSON_KEY_BALANCE);
          result = read_uuid(reader, GRDM_JSON_KEY_COMMUNITY_UUID, balance->community_uuid);
        }
        arnm_json_reader_leave(reader, element);
      }
      if (ARNM_SUCCESS == result) { tx->account_balances_count = count; }
    }
    arnm_json_reader_leave(reader, array);
    if (ARNM_SUCCESS != result) { return result; }
  }

  if (arnm_json_reader_has(reader, GRDM_JSON_KEY_ENCRYPTED_MEMOS)) {
    arnm_json_value *array = arnm_json_reader_enter(reader, GRDM_JSON_KEY_ENCRYPTED_MEMOS);
    const uint32_t count = arnm_json_reader_count(reader);
    if (count) {
      result = arnm_alloc(
          (uint8_t **)&tx->encrypted_memos, (uint32_t)(count * sizeof(grdw_encrypted_memo)),
          &tx->memory_area
      );
      for (uint32_t i = 0; i < count && ARNM_SUCCESS == result; ++i) {
        grdw_encrypted_memo *memo = &tx->encrypted_memos[i];
        arnm_json_value *element = arnm_json_reader_enter_at(reader, i);
        memo->type = GRDT_MEMO_KEY_SHARED_SECRET;
        const char *type_name = read_enum_name(reader, GRDM_JSON_KEY_TYPE);
        if (type_name) { result = memo_key_from_string(&memo->type, type_name); }
        if (ARNM_SUCCESS == result) {
          result = read_hex_block(&memo->memo, reader, GRDM_JSON_KEY_MEMO, &tx->memory_area);
        }
        arnm_json_reader_leave(reader, element);
      }
      if (ARNM_SUCCESS == result) { tx->encrypted_memos_count = count; }
    }
    arnm_json_reader_leave(reader, array);
    if (ARNM_SUCCESS != result) { return result; }
  }

  if (arnm_json_reader_has(reader, GRDM_JSON_KEY_SIGNATURE_PAIRS)) {
    arnm_json_value *array = arnm_json_reader_enter(reader, GRDM_JSON_KEY_SIGNATURE_PAIRS);
    const uint32_t count = arnm_json_reader_count(reader);
    if (count) {
      result = arnm_alloc(
          (uint8_t **)&tx->signature_pairs, (uint32_t)(count * sizeof(grdw_signature_pair)),
          &tx->memory_area
      );
      for (uint32_t i = 0; i < count && ARNM_SUCCESS == result; ++i) {
        grdw_signature_pair *pair = &tx->signature_pairs[i];
        arnm_json_value *element = arnm_json_reader_enter_at(reader, i);
        result = read_hex_fixed(
            reader, GRDM_JSON_KEY_PUBLIC_KEY, pair->public_key, SIGN_PUBLIC_KEY_SIZE
        );
        if (ARNM_SUCCESS == result) {
          result =
              read_hex_fixed(reader, GRDM_JSON_KEY_SIGNATURE, pair->signature, SIGN_SIGNATURE_SIZE);
        }
        arnm_json_reader_leave(reader, element);
      }
      if (ARNM_SUCCESS == result) { tx->signature_pairs_count = count; }
    }
    arnm_json_reader_leave(reader, array);
    if (ARNM_SUCCESS != result) { return result; }
  }

  return result;
}

/** @brief Read the two members only a transaction that is not local ever carries. */
static arnm_result read_cross_group(grdr_complete_transaction *tx, arnm_json_reader *reader) {
  const char *cross_group_name = read_enum_name(reader, GRDM_JSON_KEY_CROSS_GROUP_TYPE);
  tx->cross_group_type = GRDT_CROSS_GROUP_LOCAL;
  if (cross_group_name) {
    const arnm_result result = cross_group_from_string(&tx->cross_group_type, cross_group_name);
    if (ARNM_SUCCESS != result) { return result; }
  }

  if (arnm_json_reader_has(reader, GRDM_JSON_KEY_TX_PAIRING_COMMUNITY_UUID)) {
    arnm_result result =
        arnm_alloc(&tx->tx_pairing_community_uuid, ARNM_UUID_BINARY_SIZE, &tx->memory_area);
    if (ARNM_SUCCESS != result) { return result; }
    result =
        read_uuid(reader, GRDM_JSON_KEY_TX_PAIRING_COMMUNITY_UUID, tx->tx_pairing_community_uuid);
    if (ARNM_SUCCESS != result) { return result; }
  }

  if (arnm_json_reader_has(reader, GRDM_JSON_KEY_PAIRING_LEDGER_ANCHOR)) {
    arnm_result result = arnm_alloc(
        (uint8_t **)&tx->pairing_ledger_anchor, (uint32_t)sizeof(grdw_ledger_anchor),
        &tx->memory_area
    );
    if (ARNM_SUCCESS != result) { return result; }
    arnm_json_value *outer = arnm_json_reader_enter(reader, GRDM_JSON_KEY_PAIRING_LEDGER_ANCHOR);
    result = read_ledger_anchor(tx->pairing_ledger_anchor, reader);
    arnm_json_reader_leave(reader, outer);
    if (ARNM_SUCCESS != result) { return result; }
  }

  return ARNM_SUCCESS;
}

/** @brief Fill the whole transaction, in the order the struct declares it. */
static arnm_result read_complete_transaction(
    grdr_complete_transaction *tx, arnm_json_reader *reader
) {
  tx->tx_nr = arnm_json_reader_get_uint64(reader, GRDM_JSON_KEY_TX_NR);
  read_timestamp(&tx->confirmed_at, reader, GRDM_JSON_KEY_CONFIRMED_AT);
  read_timestamp(&tx->created_at, reader, GRDM_JSON_KEY_CREATED_AT);

  arnm_result result = read_uuid(reader, GRDM_JSON_KEY_TX_COMMUNITY_UUID, tx->tx_community_uuid);
  if (ARNM_SUCCESS != result) { return result; }

  arnm_json_value *outer = arnm_json_reader_enter(reader, GRDM_JSON_KEY_LEDGER_ANCHOR);
  result = read_ledger_anchor(&tx->ledger_anchor, reader);
  arnm_json_reader_leave(reader, outer);
  if (ARNM_SUCCESS != result) { return result; }

  const char *transaction_name = read_enum_name(reader, GRDM_JSON_KEY_TRANSACTION_TYPE);
  if (transaction_name) {
    result = transaction_from_string(&tx->transaction_type, transaction_name);
    if (ARNM_SUCCESS != result) { return result; }
  }

  const char *derivation_name = read_enum_name(reader, GRDM_JSON_KEY_BALANCE_DERIVATION_TYPE);
  if (derivation_name) {
    result = balance_derivation_from_string(&tx->balance_derivation_type, derivation_name);
    if (ARNM_SUCCESS != result) { return result; }
  }

  result =
      read_hex_fixed(reader, GRDM_JSON_KEY_TX_RUNNING_HASH, tx->tx_running_hash, GENERIC_HASH_SIZE);
  if (ARNM_SUCCESS != result) { return result; }

  // a document the reader has already refused reads as zeros from here on, and a zero
  // transaction type has no branch -- so the type's own layout is only asked for while the
  // document still speaks
  if (ARNM_SUCCESS != arnm_json_reader_status(reader)) { return ARNM_SUCCESS; }

  result = read_transaction_detail(tx, reader);
  if (ARNM_SUCCESS != result) { return result; }

  result = read_arrays(tx, reader);
  if (ARNM_SUCCESS != result) { return result; }

  result = read_cross_group(tx, reader);
  if (ARNM_SUCCESS != result) { return result; }

  return read_hex_block(&tx->body_bytes, reader, GRDM_JSON_KEY_BODY_BYTES, &tx->memory_area);
}

arnm_result grdm_complete_transaction_from_json(
    grdr_complete_transaction *tx,
    const char *json,
    uint32_t json_length,
    arnm *allocator,
    arnm_json_read_flags flags
) {
  if (!tx || !json) { return ARNM_ERROR_NULL_POINTER; }
  if (!json_length) { return ARNM_ERROR_INVALID_PARAM; }

  arnm_json_reader reader;
  arnm_result result = arnm_json_reader_init(&reader, allocator, flags);
  if (ARNM_SUCCESS != result) { return result; }

  result = arnm_json_reader_parse(&reader, json, json_length);
  if (ARNM_SUCCESS == result) {
    grdr_complete_transaction_release(tx);

    uint32_t memory_size = 0;
    result = calculate_memory_size(&memory_size, &reader);
    // the sizing pass reads nothing, but a document strange enough to make it record something
    // would leave every later reading empty. Cleared here so the fill pass below meets the
    // document as it is and records its own verdict.
    arnm_json_reader_clear_error(&reader);

    if (ARNM_SUCCESS == result && memory_size) {
      result = arnm_init_arena(&tx->memory_area, memory_size);
    }
    if (ARNM_SUCCESS == result) { result = read_complete_transaction(tx, &reader); }

    // the reader's own first refusal outranks anything decided above it: a member that was
    // missing or of the wrong type is why the fields after it read as zeros, and naming that is
    // more use than naming what the zeros then failed to be
    const arnm_result status = arnm_json_reader_status(&reader);
    if (ARNM_SUCCESS != status) { result = status; }
    if (ARNM_SUCCESS != result) { grdr_complete_transaction_release(tx); }
  }

  // the document drew from the caller's allocator; an arena that cannot take it back from its
  // tail keeps it until its own reset, which is the caller's rhythm and not this transaction's
  arnm_json_reader_release(&reader);
  return result;
}
