#include "gradido_blockchain_core/mapping/runtime_from_json.h"

#include "arnm/arena.h"
#include "arnm/converter.h"
#include "arnm/memory.h"
#include "arnm/result.h"
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
#include "gradido_blockchain_core/utils/string_helper.h"

#include <stdint.h>
#include <string.h>

/*
 * How this file reads an object, and why it does not ask for members by name.
 *
 * A JSON object keeps its members in a chain, so asking it for one key walks that chain until
 * the key turns up. Asking it for all of its keys therefore walks it once per question -- the
 * square of its length in comparisons, for an object that could have answered every question in
 * a single pass. The transaction's root object carries twenty-odd members and this mapping
 * wants nearly all of them, which is the worst shape that arithmetic has; an array of a few
 * hundred little objects is the second worst, because the same waste is paid per element.
 *
 * So nothing here asks. Every object is handed to arnm_json_read_object() with a table of the
 * members this mapping wants -- what each key is called, what it should become, and where it
 * goes -- and the walk fills them all in one pass over the chain. The table is the whole
 * description of a shape: there is no enumeration beside it to keep in step, and no loop per
 * shape to write the same way twice.
 *
 * A member that is an object or an array of its own is asked for as a value and walked with a
 * table of its own, one level at a time. That is also how the two shapes that cannot decide
 * before they have seen everything are read: the root needs `transaction_type` before it knows
 * whether `transfer` or `community_root` is the member that matters, and a ledger anchor needs
 * `type` before it knows which half of its union is there, so both take their members as values
 * first and read them afterwards. The root would want that regardless, because the same values
 * are read once to size the arena and once to fill it.
 *
 * What the walk does not decide is which members are required. It names what it found, and the
 * caller reads that answer once for the whole object -- which is where it belongs, because it
 * is the same answer whichever way the object was read.
 *
 * A reader that fails writes into its target on the way and stops where it failed. Nothing is
 * rolled back, because nothing here needs it: every reader reports through arnm_result, and a
 * caller handed an error does not go on to read what it passed in.
 */

/**
 * @brief The bit field number @p index claims in the mask a walk hands back.
 *
 * The indices of one table run from zero upwards in the order its entries are written, which is
 * the order a reader meets them in.
 */
#define SEEN(index) ((uint64_t)1u << (index))

/**
 * @brief What the mask holds once a table of @p count fields has been filled in full.
 *
 * The whole set is named by counting the entries rather than by a total typed out by hand. A
 * total that can be wrong is a required member silently made optional, or a document refused
 * that was never wrong -- and neither shows itself at the place the number was written.
 */
#define ALL_SEEN(count) ((((uint64_t)1u) << (count)) - 1u)

// ********** the shapes, and the keys each of them knows ************************************

/** @brief Members of the document's root object. */
typedef enum root_field {
  ROOT_FIELD_TX_NR = 0,
  ROOT_FIELD_CONFIRMED_AT,
  ROOT_FIELD_CREATED_AT,
  ROOT_FIELD_TX_COMMUNITY_UUID,
  ROOT_FIELD_LEDGER_ANCHOR,
  ROOT_FIELD_TRANSACTION_TYPE,
  ROOT_FIELD_BALANCE_DERIVATION_TYPE,
  ROOT_FIELD_CROSS_GROUP_TYPE,
  ROOT_FIELD_TX_RUNNING_HASH,
  ROOT_FIELD_BODY_BYTES,
  ROOT_FIELD_TRANSFER,
  ROOT_FIELD_REGISTER_ADDRESS,
  ROOT_FIELD_COMMUNITY_ROOT,
  ROOT_FIELD_TARGET_DATE,
  ROOT_FIELD_TIMEOUT_DURATION,
  ROOT_FIELD_PREVIOUS_TX,
  ROOT_FIELD_ACCOUNT_BALANCES,
  ROOT_FIELD_ENCRYPTED_MEMOS,
  ROOT_FIELD_SIGNATURE_PAIRS,
  ROOT_FIELD_TX_PAIRING_COMMUNITY_UUID,
  ROOT_FIELD_PAIRING_LEDGER_ANCHOR,
  ROOT_FIELD_COUNT
} root_field;

// ********** the small readings every field is built from **********************************

/*
 * The shapes a JSON document spells bytes in -- hex, base64, the canonical 8-4-4-4-12 -- are
 * read by arnm: @ref arnm_json_read_hex_fixed(), @ref arnm_json_read_uuid() and
 * @ref arnm_json_read_base64_block() are the string read and the conversion in one call, and
 * the length check that belongs between them is theirs to remember rather than this file's to
 * repeat per field.
 *
 * What stays here are the readings this mapping's own contract shapes. All of them take a value
 * the walk already found, so none of them searches for anything. A NULL is a member the
 * document did not carry, and every one of them refuses it: what a transaction type owns is
 * required, and a silent zero in a public key or an amount is the expensive kind of
 * forgiveness. Only the caller knows which members are optional, so only the caller tests for
 * NULL before it gets here -- and where a required member is handed straight to one of the arnm
 * reads, which answer a NULL value with ARNM_ERROR_NULL_POINTER as every arnm call does, the
 * caller tests for it there too, so that a missing member stays the ARNM_ERROR_DECODE_FAILED
 * this mapping's header promises.
 */

/**
 * @brief Read a string member, for the enumerations that arrive as their own spelling.
 *
 * The name is turned into a value by the grdt_*_from_string() of the type that owns it, so no
 * enumerator is spelled in this file and there is no second table to fall out of step with the
 * first. Each of those answers a name it does not recognise with the enumerator that means none
 * -- for cross group and memo key one that had to be added past the protobuf range to exist at
 * all -- and every caller below refuses that answer, so an unrecognised name is a refused
 * document. The one exception is the ledger anchor: its "unspecified" is a type a transaction
 * is allowed to carry, so there the two cannot be told apart and neither is refused.
 *
 * @param[out] out   Receives the bytes, borrowed from the document and NUL terminated.
 * @param[out] size  Receives the characters in @p out, which a from_string() needs to tell a
 *                   name from one that merely begins like it.
 * @param[in]  value Member to read, or NULL where the object did not carry it.
 * @retval ARNM_SUCCESS                 @p out points into the document.
 * @retval ARNM_ERROR_DECODE_FAILED     @p value is absent.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE @p value is there and is no string.
 */
static arnm_result read_enum_name(const char **out, uint32_t *size, const arnm_json_value *value) {
  if (!value) { return ARNM_ERROR_DECODE_FAILED; }
  return arnm_json_read_string(value, out, size);
}

/** @brief Read a signed 64 bit member, refusing one the document did not carry. */
static arnm_result read_int64(int64_t *out, const arnm_json_value *value) {
  if (!value) { return ARNM_ERROR_DECODE_FAILED; }
  return arnm_json_read_int64(value, out);
}

/** @brief Read an unsigned 64 bit member, refusing one the document did not carry. */
static arnm_result read_uint64(uint64_t *out, const arnm_json_value *value) {
  if (!value) { return ARNM_ERROR_DECODE_FAILED; }
  return arnm_json_read_uint64(value, out);
}

/**
 * @brief Read a timestamp object: whole seconds, and the nanos within the second.
 *
 * @param[out] out    Timestamp to fill; written as the walk meets its members, so a failed
 *                    read leaves what it had already taken.
 * @param[in]  object The object, or NULL where the enclosing walk did not find it.
 */
static arnm_result read_timestamp(grdd_timestamp *out, const arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  const arnm_json_field fields[] = {
      ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_SECONDS, &out->seconds),
      ARNM_JSON_FIELD_INT32(GRDM_JSON_KEY_NANOS, &out->nanos)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 2u, &seen, NULL);
  if (ARNM_SUCCESS != result) { return result; }
  return (ALL_SEEN(2) == seen) ? ARNM_SUCCESS : ARNM_ERROR_DECODE_FAILED;
}

/** @brief Read the three numbers of a hiero account id. */
static arnm_result read_account_id(grdw_hiero_account_id *out, const arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  const arnm_json_field fields[] = {
      ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_SHARD_NUM, &out->shardNum),
      ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_REALM_NUM, &out->realmNum),
      ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_ACCOUNT_NUM, &out->accountNum)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 3u, &seen, NULL);
  if (ARNM_SUCCESS != result) { return result; }
  return (ALL_SEEN(3) == seen) ? ARNM_SUCCESS : ARNM_ERROR_DECODE_FAILED;
}

/** @brief Read a hiero transaction id: when it was valid, and whose it was. */
static arnm_result read_hiero_transaction_id(
    grdw_hiero_transaction_id *out, const arnm_json_value *object
) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  // both members are objects of their own, so the walk hands them over and this walks them in
  // turn -- a shape described all the way down would cost more to read than it saves
  arnm_json_value *valid_start = NULL;
  arnm_json_value *account = NULL;
  const arnm_json_field fields[] = {
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_TRANSACTION_VALID_START, &valid_start),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_ACCOUNT_ID, &account)
  };
  uint64_t seen = 0;
  arnm_result result = arnm_json_read_object(object, fields, 2u, &seen, NULL);
  if (ARNM_SUCCESS != result) { return result; }
  if (ALL_SEEN(2) != seen) { return ARNM_ERROR_DECODE_FAILED; }

  result = read_timestamp(&out->transactionValidStart, valid_start);
  if (ARNM_SUCCESS != result) { return result; }
  return read_account_id(&out->accountID, account);
}

/**
 * @brief Read a ledger anchor object.
 *
 * The type decides which member of the union is there to be read, exactly as it decided which
 * one was written. Every branch writes the whole union, so no byte of it is left as the arena
 * handed it over.
 */
static arnm_result read_ledger_anchor(grdw_ledger_anchor *out, const arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  // the type decides which of the other two matters, and a walk cannot promise to have met it
  // first -- so both are taken as they come and read once the type has spoken
  const char *type_name = NULL;
  uint32_t type_size = 0;
  arnm_json_value *id = NULL;
  arnm_json_value *hiero = NULL;
  const arnm_json_field fields[] = {
      ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_TYPE, &type_name, &type_size),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_ID, &id),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_HIERO_TRANSACTION_ID, &hiero)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 3u, &seen, NULL);
  if (ARNM_SUCCESS != result) { return result; }
  if (!(seen & SEEN(0))) { return ARNM_ERROR_DECODE_FAILED; }

  // no refusal on UNSPECIFIED: it is an anchor type a transaction is allowed to have, so a name
  // that is not one of the others is read as it and the switch below says what that means
  out->id = 0;
  out->type = grdt_ledger_anchor_from_string(type_name, type_size);

  switch (out->type) {
  case GRDT_LEDGER_ANCHOR_UNSPECIFIED:
    return ARNM_SUCCESS;
  case GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID:
    return read_hiero_transaction_id(&out->hiero_transaction_id, hiero);
  default:
    return read_uint64(&out->id, id);
  }
}

// ********** the transaction's detail, branch by branch ************************************

/** @brief Read the transfer branch, which serves a creation and both deferred transfers too. */
static arnm_result read_transfer(grdr_complete_transaction *tx, const arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  const arnm_json_field fields[] = {
      ARNM_JSON_FIELD_HEX_FIXED(
          GRDM_JSON_KEY_SENDER_PUBKEY, tx->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE
      ),
      ARNM_JSON_FIELD_HEX_FIXED(
          GRDM_JSON_KEY_RECIPIENT_PUBKEY, tx->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE
      ),
      ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_AMOUNT, &tx->transfer.amount),
      ARNM_JSON_FIELD_UUID(GRDM_JSON_KEY_COIN_COMMUNITY_UUID, tx->transfer.coin_community_uuid)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 4u, &seen, NULL);
  if (ARNM_SUCCESS != result) { return result; }
  return (ALL_SEEN(4) == seen) ? ARNM_SUCCESS : ARNM_ERROR_DECODE_FAILED;
}

/** @brief Read the register-address branch, address type and derivation index included. */
static arnm_result read_register_address(
    grdr_complete_transaction *tx, const arnm_json_value *object
) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  const char *address_name = NULL;
  uint32_t address_size = 0;
  const arnm_json_field fields[] = {
      ARNM_JSON_FIELD_HEX_FIXED(
          GRDM_JSON_KEY_USER_PUBLIC_KEY, tx->register_address.user_public_key, SIGN_PUBLIC_KEY_SIZE
      ),
      ARNM_JSON_FIELD_HEX_FIXED(
          GRDM_JSON_KEY_NAME_HASH, tx->register_address.name_hash, GENERIC_HASH_SIZE
      ),
      ARNM_JSON_FIELD_HEX_FIXED(
          GRDM_JSON_KEY_ACCOUNT_PUBLIC_KEY, tx->register_address.account_public_key,
          SIGN_PUBLIC_KEY_SIZE
      ),
      // the second union, not the one above: same transaction, other half of the struct
      ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_ADDRESS_TYPE, &address_name, &address_size),
      ARNM_JSON_FIELD_UINT32(GRDM_JSON_KEY_DERIVATION_INDEX, &tx->derivation_index)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 5u, &seen, NULL);
  if (ARNM_SUCCESS != result) { return result; }
  if (ALL_SEEN(5) != seen) { return ARNM_ERROR_DECODE_FAILED; }

  tx->address_type = grdt_address_from_string(address_name, address_size);
  return (GRDT_ADDRESS_NONE == tx->address_type) ? ARNM_ERROR_ENUM_UNKNOWN : ARNM_SUCCESS;
}

/** @brief Read the community-root branch: the community key and its two account keys. */
static arnm_result read_community_root(
    grdr_complete_transaction *tx, const arnm_json_value *object
) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  const arnm_json_field fields[] = {
      ARNM_JSON_FIELD_HEX_FIXED(
          GRDM_JSON_KEY_PUBLIC_KEY, tx->community_root.public_key, SIGN_PUBLIC_KEY_SIZE
      ),
      ARNM_JSON_FIELD_HEX_FIXED(
          GRDM_JSON_KEY_GMW_PUBLIC_KEY, tx->community_root.gmw_public_key, SIGN_PUBLIC_KEY_SIZE
      ),
      ARNM_JSON_FIELD_HEX_FIXED(
          GRDM_JSON_KEY_AUF_PUBLIC_KEY, tx->community_root.auf_public_key, SIGN_PUBLIC_KEY_SIZE
      )
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 3u, &seen, NULL);
  if (ARNM_SUCCESS != result) { return result; }
  return (ALL_SEEN(3) == seen) ? ARNM_SUCCESS : ARNM_ERROR_DECODE_FAILED;
}

/**
 * @brief Read the detail and the context the transaction's type owns, and nothing else.
 *
 * Sorted by expected frequency of occurrence, the same order the wire mapping keeps. A type
 * with no layout here is refused rather than half read -- the same refusal
 * grdm_complete_transaction_from_wire() answers for the same types.
 */
static arnm_result read_transaction_detail(
    grdr_complete_transaction *tx, arnm_json_value *const *root
) {
  arnm_result result = ARNM_SUCCESS;
  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_TRANSFER:
    return read_transfer(tx, root[ROOT_FIELD_TRANSFER]);
  case GRDT_TRANSACTION_CREATION:
    result = read_transfer(tx, root[ROOT_FIELD_TRANSFER]);
    if (ARNM_SUCCESS != result) { return result; }
    return read_int64(&tx->target_date, root[ROOT_FIELD_TARGET_DATE]);
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    return read_register_address(tx, root[ROOT_FIELD_REGISTER_ADDRESS]);
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
    result = read_transfer(tx, root[ROOT_FIELD_TRANSFER]);
    if (ARNM_SUCCESS != result) { return result; }
    return read_int64(&tx->timeout_duration, root[ROOT_FIELD_TIMEOUT_DURATION]);
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    result = read_transfer(tx, root[ROOT_FIELD_TRANSFER]);
    if (ARNM_SUCCESS != result) { return result; }
    return read_uint64(&tx->previous_tx, root[ROOT_FIELD_PREVIOUS_TX]);
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER:
    return read_uint64(&tx->previous_tx, root[ROOT_FIELD_PREVIOUS_TX]);
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    return read_community_root(tx, root[ROOT_FIELD_COMMUNITY_ROOT]);
  default:
    return ARNM_ERROR_ENUM_UNHANDLED;
  }
}

// ********** sizing the transaction's own arena ********************************************

/**
 * @brief The member, or NULL where the document wrote it as the literal `null`.
 *
 * arnm's reader counts a null member and an absent one as the same thing, because for a mapper
 * they are: both say "nothing here". A walk cannot make that distinction on its own -- it files
 * whatever it finds -- so it is made once, here, and only where a member is optional. A
 * required member that arrives as `null` is refused further down by the reading that wanted a
 * number or a string of it, which is the more useful complaint anyway.
 *
 * @param[in] value Member as the walk filed it, or NULL.
 * @return @p value, or NULL where there is nothing to read.
 */
static arnm_json_value *present(arnm_json_value *value) {
  return (value && ARNM_JSON_TYPE_NULL != arnm_json_value_type(value)) ? value : NULL;
}

/** @brief Elements in @p value, refusing anything that is there and is no array. */
static arnm_result array_length(uint32_t *out, const arnm_json_value *value) {
  *out = 0;
  if (!value) { return ARNM_SUCCESS; }
  if (ARNM_JSON_TYPE_ARRAY != arnm_json_value_type(value)) { return ARNM_ERROR_INVALID_ENUM_TYPE; }
  *out = arnm_json_array_size(value);
  return ARNM_SUCCESS;
}

/**
 * @brief Bytes the memo payloads of @p array will occupy, aligned the way an arena charges.
 *
 * The one array whose elements have to be looked at before the arena exists: a balance and a
 * signature are as long as their types say, a memo is as long as its own base64. Which is why this
 * is the only array walked twice -- once here for its lengths, once below for its bytes.
 */
static arnm_result memo_payload_size(uint64_t *total, arnm_json_value *array) {
  arnm_json_array_iter iter;
  if (ARNM_SUCCESS != arnm_json_array_iter_init(array, &iter)) {
    return ARNM_ERROR_INVALID_ENUM_TYPE;
  }
  arnm_json_value *element = NULL;
  while (arnm_json_array_iter_next(&iter, &element)) {
    // only the payload is wanted here; the type this element also carries is read on the
    // second walk, where the bytes it names actually go
    const char *text = NULL;
    uint32_t length = 0;
    const arnm_json_field fields[] = {ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_MEMO, &text, &length)};
    uint64_t seen = 0;
    const arnm_result walked = arnm_json_read_object(element, fields, 1u, &seen, NULL);
    if (ARNM_SUCCESS != walked) { return walked; }
    if (ALL_SEEN(1) != seen) { return ARNM_ERROR_DECODE_FAILED; }

    uint32_t size = 0;
    const arnm_result sized = arnm_base64_binary_size(text, length, &size);
    if (ARNM_SUCCESS != sized) { return sized; }
    *total += ARNM_ALIGN8((uint64_t)size);
  }
  return ARNM_SUCCESS;
}

/**
 * @brief Bytes the arrays and the byte blocks of this document will occupy.
 *
 * The counterpart of calculate_memory_size() in the wire mapping, and it answers the same
 * question from the other bank: how much ground the transaction needs before a byte of it is
 * copied. Nothing is searched for -- the root walk already found every member this reads, so
 * this is counting and no more.
 *
 * @param[out] out  Bytes to open the arena with, aligned the way an arena charges.
 * @param[in]  root The root object's members, from @ref collect_members().
 * @retval ARNM_SUCCESS                    @p out holds the figure.
 * @retval ARNM_ERROR_DECODE_FAILED        A memo carries no hex, or an odd run of it.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE    A member is there and is of the wrong JSON type.
 * @retval ARNM_ERROR_RESOURCE_SIZE_EXCEED The sum is past what an arena can be opened with.
 */
static arnm_result calculate_memory_size(uint32_t *out, arnm_json_value *const *root) {
  uint64_t total = 0;
  uint32_t count = 0;

  count = arnm_json_array_size(root[ROOT_FIELD_ACCOUNT_BALANCES]);
  total += ARNM_ALIGN8((uint64_t)count * sizeof(grdw_account_balance));

  count = arnm_json_array_size(root[ROOT_FIELD_ENCRYPTED_MEMOS]);
  total += ARNM_ALIGN8((uint64_t)count * sizeof(grdw_encrypted_memo));
  if (count) {
    result = memo_payload_size(&total, root[ROOT_FIELD_ENCRYPTED_MEMOS]);
    if (ARNM_SUCCESS != result) { return result; }
  }

  count = arnm_json_array_size(root[ROOT_FIELD_SIGNATURE_PAIRS]);
  total += ARNM_ALIGN8((uint64_t)count * sizeof(grdw_signature_pair));

  if (present(root[ROOT_FIELD_TX_PAIRING_COMMUNITY_UUID])) {
    total += ARNM_ALIGN8((uint64_t)ARNM_UUID_BINARY_SIZE);
  }
  if (present(root[ROOT_FIELD_PAIRING_LEDGER_ANCHOR])) {
    total += ARNM_ALIGN8((uint64_t)sizeof(grdw_ledger_anchor));
  }

  if (root[ROOT_FIELD_BODY_BYTES]) {
    const char *text = NULL;
    uint32_t length = 0;
    result = arnm_json_read_string(root[ROOT_FIELD_BODY_BYTES], &text, &length);
    if (ARNM_SUCCESS != result) { return result; }
    uint32_t size = 0;
    result = arnm_base64_binary_size(text, length, &size);
    if (ARNM_SUCCESS != result) { return result; }
    total += ARNM_ALIGN8((uint64_t)size);
  }

  // the counts and the lengths came out of a document, and a document may claim more than a
  // uint32_t holds. Refusing here is what keeps a sum past 4 GiB from wrapping into a small
  // arena that every later allocation then runs past -- the same guard the wire mapping keeps.
  if (total > ARNM_MAX_ALLOC_SIZE) { return ARNM_ERROR_RESOURCE_SIZE_EXCEED; }
  *out = (uint32_t)total;
  return ARNM_SUCCESS;
}

// ********** the arrays ********************************************************************

/** @brief Read the balances as they settled. */
static arnm_result read_account_balances(
    grdr_complete_transaction *tx, const arnm_json_value *array
) {
  uint32_t count = 0;
  arnm_result result = array_length(&count, array);
  if (ARNM_SUCCESS != result || !count) { return result; }

  // the cast is what calculate_memory_size() already bounded: the arena could not have been
  // opened if this product did not fit
  result = arnm_alloc(
      (uint8_t **)&tx->account_balances, (uint32_t)(count * sizeof(grdw_account_balance)),
      &tx->memory_area
  );
  if (ARNM_SUCCESS != result) { return result; }

  arnm_json_array_iter iter;
  if (ARNM_SUCCESS != arnm_json_array_iter_init(array, &iter)) {
    return ARNM_ERROR_INVALID_ENUM_TYPE;
  }
  arnm_json_value *element = NULL;
  uint32_t filled = 0;
  while (filled < count && arnm_json_array_iter_next(&iter, &element)) {
    grdw_account_balance *balance = &tx->account_balances[filled];
    const arnm_json_field fields[] = {
        ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_PUBKEY, balance->pubkey, SIGN_PUBLIC_KEY_SIZE),
        ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_BALANCE, &balance->balance),
        ARNM_JSON_FIELD_UUID(GRDM_JSON_KEY_COMMUNITY_UUID, balance->community_uuid)
    };
    uint64_t seen = 0;
    result = arnm_json_read_object(element, fields, 3u, &seen, NULL);
    if (ARNM_SUCCESS != result) { return result; }
    if (ALL_SEEN(3) != seen) { return ARNM_ERROR_DECODE_FAILED; }
    ++filled;
  }
  tx->account_balances_count = filled;
  return ARNM_SUCCESS;
}

/** @brief Read the memos, each into a block of its own. */
static arnm_result read_encrypted_memos(
    grdr_complete_transaction *tx, const arnm_json_value *array
) {
  uint32_t count = 0;
  arnm_result result = array_length(&count, array);
  if (ARNM_SUCCESS != result || !count) { return result; }

  result = arnm_alloc(
      (uint8_t **)&tx->encrypted_memos, (uint32_t)(count * sizeof(grdw_encrypted_memo)),
      &tx->memory_area
  );
  if (ARNM_SUCCESS != result) { return result; }

  arnm_json_array_iter iter;
  if (ARNM_SUCCESS != arnm_json_array_iter_init(array, &iter)) {
    return ARNM_ERROR_INVALID_ENUM_TYPE;
  }
  arnm_json_value *element = NULL;
  uint32_t filled = 0;
  while (filled < count && arnm_json_array_iter_next(&iter, &element)) {
    grdw_encrypted_memo *memo = &tx->encrypted_memos[filled];
    const char *type_name = NULL;
    uint32_t type_size = 0;
    const arnm_json_field fields[] = {
        ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_TYPE, &type_name, &type_size),
        ARNM_JSON_FIELD_BASE64_BLOCK(GRDM_JSON_KEY_MEMO, &memo->memo)
    };
    uint64_t seen = 0;
    result = arnm_json_read_object(element, fields, 2u, &seen, &tx->memory_area);
    if (ARNM_SUCCESS != result) { return result; }
    if (ALL_SEEN(2) != seen) { return ARNM_ERROR_DECODE_FAILED; }

    memo->type = grdt_memo_key_from_string(type_name, type_size);
    if (GRDT_MEMO_KEY_NONE == memo->type) { return ARNM_ERROR_ENUM_UNKNOWN; }
    ++filled;
  }
  tx->encrypted_memos_count = filled;
  return ARNM_SUCCESS;
}

/** @brief Read the signatures over the body bytes. */
static arnm_result read_signature_pairs(
    grdr_complete_transaction *tx, const arnm_json_value *array
) {
  uint32_t count = 0;
  arnm_result result = array_length(&count, array);
  if (ARNM_SUCCESS != result || !count) { return result; }

  result = arnm_alloc(
      (uint8_t **)&tx->signature_pairs, (uint32_t)(count * sizeof(grdw_signature_pair)),
      &tx->memory_area
  );
  if (ARNM_SUCCESS != result) { return result; }

  arnm_json_array_iter iter;
  if (ARNM_SUCCESS != arnm_json_array_iter_init(array, &iter)) {
    return ARNM_ERROR_INVALID_ENUM_TYPE;
  }
  arnm_json_value *element = NULL;
  uint32_t filled = 0;
  while (filled < count && arnm_json_array_iter_next(&iter, &element)) {
    grdw_signature_pair *pair = &tx->signature_pairs[filled];
    const arnm_json_field fields[] = {
        ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_PUBLIC_KEY, pair->public_key, SIGN_PUBLIC_KEY_SIZE),
        ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_SIGNATURE, pair->signature, SIGN_SIGNATURE_SIZE)
    };
    uint64_t seen = 0;
    result = arnm_json_read_object(element, fields, 2u, &seen, NULL);
    if (ARNM_SUCCESS != result) { return result; }
    if (ALL_SEEN(2) != seen) { return ARNM_ERROR_DECODE_FAILED; }
    ++filled;
  }
  tx->signature_pairs_count = filled;
  return ARNM_SUCCESS;
}

/** @brief Read the two members only a transaction that is not local ever carries. */
static arnm_result read_cross_group(grdr_complete_transaction *tx, arnm_json_value *const *root) {
  const char *cross_group_name = NULL;
  uint32_t cross_group_size = 0;
  arnm_result result =
      read_enum_name(&cross_group_name, &cross_group_size, root[ROOT_FIELD_CROSS_GROUP_TYPE]);
  if (ARNM_SUCCESS != result) { return result; }
  tx->cross_group_type = grdt_cross_group_from_string(cross_group_name, cross_group_size);
  if (GRDT_CROSS_GROUP_NONE == tx->cross_group_type) { return ARNM_ERROR_ENUM_UNKNOWN; }

  if (present(root[ROOT_FIELD_TX_PAIRING_COMMUNITY_UUID])) {
    result = arnm_alloc(&tx->tx_pairing_community_uuid, ARNM_UUID_BINARY_SIZE, &tx->memory_area);
    if (ARNM_SUCCESS != result) { return result; }
    result = arnm_json_read_uuid(
        present(root[ROOT_FIELD_TX_PAIRING_COMMUNITY_UUID]), tx->tx_pairing_community_uuid
    );
    if (ARNM_SUCCESS != result) { return result; }
  }

  if (present(root[ROOT_FIELD_PAIRING_LEDGER_ANCHOR])) {
    result = arnm_alloc(
        (uint8_t **)&tx->pairing_ledger_anchor, (uint32_t)sizeof(grdw_ledger_anchor),
        &tx->memory_area
    );
    if (ARNM_SUCCESS != result) { return result; }
    result = read_ledger_anchor(
        tx->pairing_ledger_anchor, present(root[ROOT_FIELD_PAIRING_LEDGER_ANCHOR])
    );
    if (ARNM_SUCCESS != result) { return result; }
  }

  return ARNM_SUCCESS;
}

/** @brief Fill the whole transaction from the root's members, in the order the struct declares. */
static arnm_result read_complete_transaction(
    grdr_complete_transaction *tx, arnm_json_value *const *root
) {
  arnm_result result = read_uint64(&tx->tx_nr, root[ROOT_FIELD_TX_NR]);
  if (ARNM_SUCCESS != result) { return result; }
  result = read_timestamp(&tx->confirmed_at, root[ROOT_FIELD_CONFIRMED_AT]);
  if (ARNM_SUCCESS != result) { return result; }
  result = read_timestamp(&tx->created_at, root[ROOT_FIELD_CREATED_AT]);
  if (ARNM_SUCCESS != result) { return result; }
  if (!root[ROOT_FIELD_TX_COMMUNITY_UUID]) { return ARNM_ERROR_DECODE_FAILED; }
  result = arnm_json_read_uuid(root[ROOT_FIELD_TX_COMMUNITY_UUID], tx->tx_community_uuid);
  if (ARNM_SUCCESS != result) { return result; }
  result = read_ledger_anchor(&tx->ledger_anchor, root[ROOT_FIELD_LEDGER_ANCHOR]);
  if (ARNM_SUCCESS != result) { return result; }

  const char *transaction_name = NULL;
  uint32_t transaction_size = 0;
  result = read_enum_name(&transaction_name, &transaction_size, root[ROOT_FIELD_TRANSACTION_TYPE]);
  if (ARNM_SUCCESS != result) { return result; }
  tx->transaction_type = grdt_transaction_from_string(transaction_name, transaction_size);
  if (GRDT_TRANSACTION_NONE == tx->transaction_type) { return ARNM_ERROR_ENUM_UNKNOWN; }

  const char *derivation_name = NULL;
  uint32_t derivation_size = 0;
  result =
      read_enum_name(&derivation_name, &derivation_size, root[ROOT_FIELD_BALANCE_DERIVATION_TYPE]);
  if (ARNM_SUCCESS != result) { return result; }
  tx->balance_derivation_type =
      grdt_balance_derivation_from_string(derivation_name, derivation_size);
  if (GRDT_BALANCE_DERIVATION_UNSPECIFIED == tx->balance_derivation_type) {
    return ARNM_ERROR_ENUM_UNKNOWN;
  }

  if (!root[ROOT_FIELD_TX_RUNNING_HASH]) { return ARNM_ERROR_DECODE_FAILED; }
  result = arnm_json_read_hex_fixed(
      root[ROOT_FIELD_TX_RUNNING_HASH], tx->tx_running_hash, GENERIC_HASH_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }

  result = read_transaction_detail(tx, root);
  if (ARNM_SUCCESS != result) { return result; }

  result = read_account_balances(tx, present(root[ROOT_FIELD_ACCOUNT_BALANCES]));
  if (ARNM_SUCCESS != result) { return result; }
  result = read_encrypted_memos(tx, present(root[ROOT_FIELD_ENCRYPTED_MEMOS]));
  if (ARNM_SUCCESS != result) { return result; }
  result = read_signature_pairs(tx, present(root[ROOT_FIELD_SIGNATURE_PAIRS]));
  if (ARNM_SUCCESS != result) { return result; }

  result = read_cross_group(tx, root);
  if (ARNM_SUCCESS != result) { return result; }

  if (!root[ROOT_FIELD_BODY_BYTES]) { return ARNM_ERROR_DECODE_FAILED; }
  return arnm_json_read_base64_block(
      &tx->body_bytes, root[ROOT_FIELD_BODY_BYTES], &tx->memory_area
  );
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

    // one walk over the root, and every member this mapping wants is in hand; nothing below
    // searches the document again
    arnm_json_value *root[ROOT_FIELD_COUNT] = {NULL};
    const arnm_json_field fields[] = {
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_TX_NR, &root[ROOT_FIELD_TX_NR]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_CONFIRMED_AT, &root[ROOT_FIELD_CONFIRMED_AT]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_CREATED_AT, &root[ROOT_FIELD_CREATED_AT]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_TX_COMMUNITY_UUID, &root[ROOT_FIELD_TX_COMMUNITY_UUID]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_LEDGER_ANCHOR, &root[ROOT_FIELD_LEDGER_ANCHOR]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_TRANSACTION_TYPE, &root[ROOT_FIELD_TRANSACTION_TYPE]),
        ARNM_JSON_FIELD_VALUE(
            GRDM_JSON_KEY_BALANCE_DERIVATION_TYPE, &root[ROOT_FIELD_BALANCE_DERIVATION_TYPE]
        ),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_CROSS_GROUP_TYPE, &root[ROOT_FIELD_CROSS_GROUP_TYPE]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_TX_RUNNING_HASH, &root[ROOT_FIELD_TX_RUNNING_HASH]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_BODY_BYTES, &root[ROOT_FIELD_BODY_BYTES]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_TRANSFER, &root[ROOT_FIELD_TRANSFER]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_REGISTER_ADDRESS, &root[ROOT_FIELD_REGISTER_ADDRESS]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_COMMUNITY_ROOT, &root[ROOT_FIELD_COMMUNITY_ROOT]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_TARGET_DATE, &root[ROOT_FIELD_TARGET_DATE]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_TIMEOUT_DURATION, &root[ROOT_FIELD_TIMEOUT_DURATION]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_PREVIOUS_TX, &root[ROOT_FIELD_PREVIOUS_TX]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_ACCOUNT_BALANCES, &root[ROOT_FIELD_ACCOUNT_BALANCES]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_ENCRYPTED_MEMOS, &root[ROOT_FIELD_ENCRYPTED_MEMOS]),
        ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_SIGNATURE_PAIRS, &root[ROOT_FIELD_SIGNATURE_PAIRS]),
        ARNM_JSON_FIELD_VALUE(
            GRDM_JSON_KEY_TX_PAIRING_COMMUNITY_UUID, &root[ROOT_FIELD_TX_PAIRING_COMMUNITY_UUID]
        ),
        ARNM_JSON_FIELD_VALUE(
            GRDM_JSON_KEY_PAIRING_LEDGER_ANCHOR, &root[ROOT_FIELD_PAIRING_LEDGER_ANCHOR]
        )
    };
    result =
        arnm_json_read_object(arnm_json_reader_root(&reader), fields, ROOT_FIELD_COUNT, NULL, NULL);

    uint32_t memory_size = 0;
    if (ARNM_SUCCESS == result) { result = calculate_memory_size(&memory_size, root); }
    if (ARNM_SUCCESS == result && memory_size) {
      result = arnm_init_arena(&tx->memory_area, memory_size);
    }
    if (ARNM_SUCCESS == result) { result = read_complete_transaction(tx, root); }
    if (ARNM_SUCCESS != result) { grdr_complete_transaction_release(tx); }
  }

  // the document drew from the caller's allocator; an arena that cannot take it back from its
  // tail keeps it until its own reset, which is the caller's rhythm and not this transaction's
  arnm_json_reader_release(&reader);
  return result;
}
