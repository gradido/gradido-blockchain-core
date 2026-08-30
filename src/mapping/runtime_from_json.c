#include "gradido_blockchain_core/mapping/runtime_from_json.h"

#include "arnm/arena.h"
#include "arnm/converter.h"
#include "arnm/memory.h"
#include "arnm/memory_block.h"
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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * How this file reads a document, and what arnm 0.7.5 decides for it.
 *
 * A JSON object keeps its members in a chain, so asking it for one key walks that chain until the
 * key turns up. Asking it for all of its keys therefore walks it once per question -- the square
 * of its length in comparisons, for an object that could have answered every question in a single
 * pass. The transaction's root object carries twenty-odd members and this mapping wants nearly all
 * of them, which is the worst shape that arithmetic has; an array of a few hundred little objects
 * is the second worst, because the same waste is paid per element.
 *
 * So nothing here asks. Every object is handed to arnm_json_read_object() with a table of the
 * members this mapping wants -- what each key is called, what it should become, and where it goes
 * -- and the walk fills them all in one pass over the chain. The table is the whole description of
 * a shape: there is no enumeration beside it to keep in step, and no loop per shape to write the
 * same way twice.
 *
 * ### The one thing a table cannot do, and what follows from it
 *
 * A table converts a member where it stands. It cannot convert one afterwards: arnm hands out a
 * value only as a handle, and the only two things that take a handle are the object walk and
 * arnm_json_read_array(). A scalar that is taken as a handle is therefore a scalar that can never
 * be read at all.
 *
 * That decides where each root member goes. A member this mapping always wants is converted in the
 * root table, straight into the transaction. A member that is an object or an array is taken as a
 * handle and walked with a table -- or a buffer -- of its own. And a scalar that only the
 * transaction's type knows whether it wants -- `target_date`, `timeout_duration`, `previous_tx` --
 * is left out of the root table entirely and asked for in a one-field walk of the root once the
 * type has spoken. That walk stops at the key it wants, and only the types that own such a scalar
 * ever pay for it.
 *
 * ### Optional members, and what a document may put in them
 *
 * The three arrays and the two pairing members are optional. arnm 0.7.5 has no way to ask a value
 * what it is, so `null` in one of them cannot be told from a number: both come back as
 * ARNM_ERROR_INVALID_ENUM_TYPE from the read that wanted an array or an object. An optional member
 * that will not read as its shape is therefore taken as absent, whichever of the two it was. That
 * is the reading a mapper wants for `null` -- "nothing here", the same as leaving the member out --
 * and it is why `"account_balances": 7` is passed over rather than refused.
 *
 * Everything a transaction type owns is required, and a missing one is refused rather than
 * defaulted: a silent zero in a public key or an amount is the expensive kind of forgiveness.
 *
 * ### The arena, and the two passes it costs
 *
 * The transaction's own arena is opened once, at the size the document says it needs, before a
 * byte is copied into it. Everything but the memo payloads can be counted from the element handles
 * alone; a memo is as long as its own base64 says, so the memos are the one array walked twice --
 * once for its lengths and once for its bytes.
 *
 * A reader that fails writes into its target on the way and stops where it failed. Nothing is
 * rolled back, because nothing here needs it: the caller releases the transaction on any refusal,
 * and every reader reports through arnm_result.
 */

/**
 * @brief The bit a field of index @p index claims in the mask a walk hands back.
 *
 * The indices of one table run from zero upwards in the order its entries are written, which is
 * the order a reader meets them in.
 */
#define SEEN(index) ((uint64_t)1u << (index))

/**
 * @brief What the mask holds once a table of @p count fields has been filled in full.
 *
 * The whole set is named by counting the entries rather than by a total typed out by hand. A total
 * that can be wrong is a required member silently made optional, or a document refused that was
 * never wrong -- and neither shows itself at the place the number was written.
 */
#define ALL_SEEN(count) ((((uint64_t)1u) << (count)) - 1u)

/** @brief A string a walk borrowed from the document, as the from_string() calls want it. */
static const char *chars(const arnm_memory_block *string) {
  return (const char *)string->data;
}

// ********** the shapes below the root ******************************************************

/**
 * @brief Read a timestamp object: whole seconds, and the nanos within the second.
 *
 * @param[out] out    Timestamp to fill; written as the walk meets its members, so a failed read
 *                    leaves what it had already taken.
 * @param[in]  object The object, or NULL where the enclosing walk did not find it.
 */
static arnm_result read_timestamp(grdd_timestamp *out, arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  arnm_json_field fields[] = {
      ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_SECONDS, &out->seconds),
      ARNM_JSON_FIELD_INT32(GRDM_JSON_KEY_NANOS, &out->nanos)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 2u, &seen);
  if (ARNM_SUCCESS != result) { return result; }
  return (ALL_SEEN(2) == seen) ? ARNM_SUCCESS : ARNM_ERROR_DECODE_FAILED;
}

/** @brief Read the three numbers of a hiero account id. */
static arnm_result read_account_id(grdw_hiero_account_id *out, arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  arnm_json_field fields[] = {
      ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_SHARD_NUM, &out->shardNum),
      ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_REALM_NUM, &out->realmNum),
      ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_ACCOUNT_NUM, &out->accountNum)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 3u, &seen);
  if (ARNM_SUCCESS != result) { return result; }
  return (ALL_SEEN(3) == seen) ? ARNM_SUCCESS : ARNM_ERROR_DECODE_FAILED;
}

/** @brief Read a hiero transaction id: when it was valid, and whose it was. */
static arnm_result read_hiero_transaction_id(
    grdw_hiero_transaction_id *out, arnm_json_value *object
) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  // both members are objects of their own, so the walk hands them over and this walks them in
  // turn -- a shape described all the way down would cost more to read than it saves
  arnm_json_value *valid_start = NULL;
  arnm_json_value *account = NULL;
  arnm_json_field fields[] = {
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_TRANSACTION_VALID_START, &valid_start),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_ACCOUNT_ID, &account)
  };
  uint64_t seen = 0;
  arnm_result result = arnm_json_read_object(object, fields, 2u, &seen);
  if (ARNM_SUCCESS != result) { return result; }
  if (ALL_SEEN(2) != seen) { return ARNM_ERROR_DECODE_FAILED; }

  result = read_timestamp(&out->transactionValidStart, valid_start);
  if (ARNM_SUCCESS != result) { return result; }
  return read_account_id(&out->accountID, account);
}

/**
 * @brief Read a ledger anchor object.
 *
 * The type decides which member of the union is there to be read, exactly as it decided which one
 * was written -- so the type is taken as a name and the other two as they come, and the switch
 * runs once the walk is over. The whole struct is cleared first, because only one branch of the
 * union is written and the rest of it may be handed on to a caller that copies all of it.
 *
 * @param[out] out    Anchor to fill; cleared before anything is read into it.
 * @param[in]  object The object, or NULL where the enclosing walk did not find it.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE @p object is no object -- which is what an optional anchor
 *                                      written as `null` looks like from here.
 */
static arnm_result read_ledger_anchor(grdw_ledger_anchor *out, arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  arnm_memory_block type_name = {NULL, 0};
  arnm_json_value *hiero = NULL;
  uint64_t id = 0;
  arnm_json_field fields[] = {
      ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_TYPE, &type_name),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_HIERO_TRANSACTION_ID, &hiero),
      ARNM_JSON_FIELD_UINT64(GRDM_JSON_KEY_ID, &id)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 3u, &seen);
  if (ARNM_SUCCESS != result) { return result; }
  if (!(seen & SEEN(0))) { return ARNM_ERROR_DECODE_FAILED; }

  memset(out, 0, sizeof(*out));
  // no refusal on UNSPECIFIED: it is an anchor type a transaction is allowed to have, so a name
  // that is not one of the others is read as it and the switch below says what that means
  out->type = grdt_ledger_anchor_from_string(chars(&type_name), type_name.size);

  switch (out->type) {
  case GRDT_LEDGER_ANCHOR_UNSPECIFIED:
    return ARNM_SUCCESS;
  case GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID:
    return read_hiero_transaction_id(&out->hiero_transaction_id, hiero);
  default:
    if (!(seen & SEEN(2))) { return ARNM_ERROR_DECODE_FAILED; }
    out->id = id;
    return ARNM_SUCCESS;
  }
}

// ********** the transaction's detail, branch by branch *************************************

/** @brief Read the transfer branch, which serves a creation and both deferred transfers too. */
static arnm_result read_transfer(grdr_complete_transaction *tx, arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  // a decoding entry is handed the buffer it writes into and measures the string against its
  // size, so each of these is the field itself and the compiler's word for how long it is
  arnm_memory_block sender = ARNM_JSON_BLOCK_OF(tx->transfer.sender_pubkey);
  arnm_memory_block recipient = ARNM_JSON_BLOCK_OF(tx->transfer.recipient_pubkey);
  arnm_memory_block coin_community = ARNM_JSON_BLOCK_OF(tx->transfer.coin_community_uuid);
  arnm_json_field fields[] = {
      ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_SENDER_PUBKEY, &sender),
      ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_RECIPIENT_PUBKEY, &recipient),
      ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_AMOUNT, &tx->transfer.amount),
      ARNM_JSON_FIELD_UUID(GRDM_JSON_KEY_COIN_COMMUNITY_UUID, &coin_community)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 4u, &seen);
  if (ARNM_SUCCESS != result) { return result; }
  return (ALL_SEEN(4) == seen) ? ARNM_SUCCESS : ARNM_ERROR_DECODE_FAILED;
}

/** @brief Read the register-address branch, address type and derivation index included. */
static arnm_result read_register_address(grdr_complete_transaction *tx, arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  arnm_memory_block user_public_key = ARNM_JSON_BLOCK_OF(tx->register_address.user_public_key);
  arnm_memory_block name_hash = ARNM_JSON_BLOCK_OF(tx->register_address.name_hash);
  arnm_memory_block account_public_key =
      ARNM_JSON_BLOCK_OF(tx->register_address.account_public_key);
  arnm_json_field fields[] = {
      ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_USER_PUBLIC_KEY, &user_public_key),
      ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_NAME_HASH, &name_hash),
      ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_ACCOUNT_PUBLIC_KEY, &account_public_key),
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 3u, &seen);
  if (ARNM_SUCCESS != result) { return result; }
  if (ALL_SEEN(3) != seen) { return ARNM_ERROR_DECODE_FAILED; }
  return ARNM_SUCCESS;
}

/** @brief Read the community-root branch: the community key and its two account keys. */
static arnm_result read_community_root(grdr_complete_transaction *tx, arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  arnm_memory_block public_key = ARNM_JSON_BLOCK_OF(tx->community_root.public_key);
  arnm_memory_block gmw_public_key = ARNM_JSON_BLOCK_OF(tx->community_root.gmw_public_key);
  arnm_memory_block auf_public_key = ARNM_JSON_BLOCK_OF(tx->community_root.auf_public_key);
  arnm_json_field fields[] = {
      ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_PUBLIC_KEY, &public_key),
      ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_GMW_PUBLIC_KEY, &gmw_public_key),
      ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_AUF_PUBLIC_KEY, &auf_public_key)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 3u, &seen);
  if (ARNM_SUCCESS != result) { return result; }
  return (ALL_SEEN(3) == seen) ? ARNM_SUCCESS : ARNM_ERROR_DECODE_FAILED;
}

// ********** the root, walked once **********************************************************


/**
 * @brief What the root walk leaves for the reading that follows it.
 *
 * The members that convert where they stand are not here -- they went straight into the
 * transaction. What is left is the objects and arrays, which are read one level at a time, the
 * three names an enumeration arrives as, and the base64 of the body, which cannot be decoded
 * before the arena that will hold it has been sized.
 */
typedef struct root_view {
  arnm_json_value *root;
  arnm_json_value *confirmed_at;
  arnm_json_value *created_at;
  arnm_json_value *ledger_anchor;
  arnm_json_value *transfer;
  arnm_json_value *register_address;
  arnm_json_value *community_root;
  arnm_json_value *account_balances;
  arnm_json_value *encrypted_memos;
  arnm_json_value *signature_pairs;
  arnm_memory_block tx_pairing_community_uuid;
  arnm_json_value *pairing_ledger_anchor;
  arnm_memory_block transaction_type;
  arnm_memory_block balance_derivation_type;
  arnm_memory_block cross_group_type;
  arnm_memory_block body_bytes;
} root_view;

/**
 * @brief Walk the root once, converting what can be converted and filing the rest.
 *
 * @param[out]    view Receives the handles and the borrowed strings; every field is written.
 * @param[in,out] tx   Receives the three members that convert straight into it.
 * @param[in]     root The document's root; not NULL.
 * @retval ARNM_ERROR_DECODE_FAILED     A member every document must carry is not there.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE The root is no object, or a member is of another JSON type
 *                                      than the field it names.
 */
static arnm_result read_root(
    root_view *view,
    grdr_complete_transaction *tx,
    arnm_json_value *root,
    grdt_transaction transaction_type
) {
  view->root = root;

  arnm_memory_block tx_community_uuid = ARNM_JSON_BLOCK_OF(tx->tx_community_uuid);
  arnm_memory_block tx_running_hash = ARNM_JSON_BLOCK_OF(tx->tx_running_hash);
  arnm_memory_block address_name = {NULL, 0};
  uint8_t field_count = 0;
  arnm_json_field fields[18];
  fields[field_count++] = (arnm_json_field)ARNM_JSON_FIELD_UINT64(GRDM_JSON_KEY_TX_NR, &tx->tx_nr);
  fields[field_count++] =
      (arnm_json_field)ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_CONFIRMED_AT, &view->confirmed_at);
  fields[field_count++] =
      (arnm_json_field)ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_CREATED_AT, &view->created_at);
  fields[field_count++] =
      (arnm_json_field)ARNM_JSON_FIELD_UUID(GRDM_JSON_KEY_TX_COMMUNITY_UUID, &tx_community_uuid);
  fields[field_count++] =
      (arnm_json_field)ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_LEDGER_ANCHOR, &view->ledger_anchor);
  if (GRDT_TRANSACTION_TRANSFER == transaction_type ||
      GRDT_TRANSACTION_CREATION == transaction_type ||
      GRDT_TRANSACTION_DEFERRED_TRANSFER == transaction_type ||
      GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER == transaction_type) {
    fields[field_count++] =
        (arnm_json_field)ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_TRANSFER, &view->transfer);
  } else if (GRDT_TRANSACTION_REGISTER_ADDRESS == transaction_type) {
    fields[field_count++] = (arnm_json_field)ARNM_JSON_FIELD_VALUE(
        GRDM_JSON_KEY_REGISTER_ADDRESS, &view->register_address
    );
  } else if (GRDT_TRANSACTION_COMMUNITY_ROOT == transaction_type) {
    fields[field_count++] =
        (arnm_json_field)ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_COMMUNITY_ROOT, &view->community_root);
  }

  if (GRDT_TRANSACTION_CREATION == transaction_type) {
    fields[field_count++] =
        (arnm_json_field)ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_TARGET_DATE, &tx->target_date);
  } else if (GRDT_TRANSACTION_DEFERRED_TRANSFER == transaction_type) {
    fields[field_count++] = (arnm_json_field)ARNM_JSON_FIELD_INT64(
        GRDM_JSON_KEY_TIMEOUT_DURATION, &tx->timeout_duration
    );
  } else if (
      GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER == transaction_type ||
      GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER == transaction_type) {
    fields[field_count++] =
        (arnm_json_field)ARNM_JSON_FIELD_UINT64(GRDM_JSON_KEY_PREVIOUS_TX, &tx->previous_tx);
  } else if (GRDT_TRANSACTION_REGISTER_ADDRESS == transaction_type) {
    fields[field_count++] =
        (arnm_json_field)ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_ADDRESS_TYPE, &address_name);
    fields[field_count++] = (arnm_json_field)ARNM_JSON_FIELD_UINT32(
        GRDM_JSON_KEY_DERIVATION_INDEX, &tx->derivation_index
    );
  }
  fields[field_count++] = (arnm_json_field)ARNM_JSON_FIELD_STRING(
      GRDM_JSON_KEY_BALANCE_DERIVATION_TYPE, &view->balance_derivation_type
  );
  fields[field_count++] =
      (arnm_json_field)ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_TX_RUNNING_HASH, &tx_running_hash);
  fields[field_count++] = (arnm_json_field)ARNM_JSON_FIELD_VALUE(
      GRDM_JSON_KEY_ACCOUNT_BALANCES, &view->account_balances
  );
  fields[field_count++] =
      (arnm_json_field)ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_ENCRYPTED_MEMOS, &view->encrypted_memos);
  fields[field_count++] =
      (arnm_json_field)ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_SIGNATURE_PAIRS, &view->signature_pairs);
  fields[field_count++] = (arnm_json_field)ARNM_JSON_FIELD_STRING(
      GRDM_JSON_KEY_CROSS_GROUP_TYPE, &view->cross_group_type
  );
  fields[field_count++] = (arnm_json_field)ARNM_JSON_FIELD_STRING(
      GRDM_JSON_KEY_TX_PAIRING_COMMUNITY_UUID, &view->tx_pairing_community_uuid
  );
  fields[field_count++] = (arnm_json_field)ARNM_JSON_FIELD_VALUE(
      GRDM_JSON_KEY_PAIRING_LEDGER_ANCHOR, &view->pairing_ledger_anchor
  );
  fields[field_count++] =
      (arnm_json_field)ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_BODY_BYTES, &view->body_bytes);

  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(root, fields, field_count, &seen);
  if (ARNM_SUCCESS != result) { return result; }
  if (GRDT_TRANSACTION_REGISTER_ADDRESS == transaction_type) {
    tx->address_type = grdt_address_from_string((const char *)address_name.data, address_name.size);
    if (GRDT_ADDRESS_NONE == tx->address_type) return ARNM_ERROR_ENUM_UNKNOWN;
  }
  return (field_count == (seen & field_count)) ? ARNM_SUCCESS : ARNM_ERROR_DECODE_FAILED;
}

/**
 * @brief Read the detail and the context the transaction's type owns, and nothing else.
 *
 * Sorted by expected frequency of occurrence, the same order the wire mapping keeps. A type with
 * no layout here is refused rather than half read -- the same refusal
 * grdm_complete_transaction_from_wire() answers for the same types.
 */
static arnm_result read_transaction_detail(grdr_complete_transaction *tx, const root_view *view) {
  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_TRANSFER:
  case GRDT_TRANSACTION_CREATION:
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    return read_transfer(tx, view->transfer);
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    return read_register_address(tx, view->register_address);
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    return read_community_root(tx, view->community_root);
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER:
    return ARNM_SUCCESS;
  default:
    return ARNM_ERROR_ENUM_UNHANDLED;
  }
}

// ********** an array, as the handles of its elements ***************************************

/**
 * @brief Elements a list holds without asking the allocator for anything.
 *
 * arnm hands an array over all at once or not at all, and it does not say how long one was that
 * did not fit -- so a list that outgrows this is read again into a buffer sized by the document's
 * node count, which no array of it can pass. Wide enough that a transaction of a real ledger never
 * reaches the second read, and small enough that three of these on the stack are a few hundred
 * bytes.
 */
#define ELEMENTS_INLINE 5u

/** @brief One array's elements: handles, how many, and where they are kept. */
typedef struct element_list {
  arnm_json_value **values;  /**< @ref inline_values, or the widened buffer once there is one. */
  uint32_t count;            /**< Elements in @ref values. */
  arnm_memory_block widened; /**< Empty unless the inline room was not enough. */
  arnm_json_value *inline_values[ELEMENTS_INLINE];
} element_list;

/** @brief Leave @p list empty and owning nothing, which is also what an absent array reads as. */
static void init_elements(element_list *list) {
  list->values = list->inline_values;
  list->count = 0;
  list->widened.data = NULL;
  list->widened.size = 0;
}

/**
 * @brief Take the elements of @p array, widening once where there are more than the room here.
 *
 * @param[out]    list        Receives the handles; left empty where there is nothing to take.
 * @param[in]     array       The member as the root walk filed it, or NULL where it was absent.
 * @param[in]     upper_bound Nodes in the document, which is a bound no array of it can pass.
 * @param[in,out] allocator   Where a widened buffer comes from; released by
 *                            @ref release_elements().
 * @retval ARNM_SUCCESS @p list holds the elements -- or nothing, where the member was absent or
 *                      was not an array at all, which for an optional member says the same thing.
 */
static arnm_result read_elements(
    element_list *list, arnm_json_value *array, uint32_t upper_bound, arnm *allocator
) {
  init_elements(list);
  if (!array) { return ARNM_SUCCESS; }

  arnm_result result = arnm_json_read_array(array, list->values, ELEMENTS_INLINE, &list->count);
  // `null`, a number, an object: an optional member that is not an array carries nothing this
  // mapping can read, and arnm 0.7.5 answers all three the same way
  if (ARNM_ERROR_INVALID_ENUM_TYPE == result) { return ARNM_SUCCESS; }
  if (ARNM_ERROR_DESTINATION_BUFFER_TO_SMALL != result) { return result; }

  if (upper_bound > ARNM_MAX_ALLOC_SIZE / (uint32_t)sizeof(arnm_json_value *)) {
    return ARNM_ERROR_RESOURCE_SIZE_EXCEED;
  }
  result = arnm_memory_block_alloc(
      &list->widened, upper_bound * (uint32_t)sizeof(arnm_json_value *), allocator
  );
  if (ARNM_SUCCESS != result) { return result; }
  list->values = (arnm_json_value **)(void *)list->widened.data;
  return arnm_json_read_array(array, list->values, upper_bound, &list->count);
}

/** @brief Give a widened buffer back, and leave the list empty either way. */
static void release_elements(element_list *list, arnm *allocator) {
  // an arena that cannot take the buffer back from its tail keeps it until its own reset, which
  // is the caller's rhythm and not this transaction's
  if (list->widened.data) { (void)arnm_memory_block_free(&list->widened, allocator); }
  init_elements(list);
}

/** @brief Read the memos, each into a block of its own. */
static arnm_result read_encrypted_memos(grdw_encrypted_memo *memos, const element_list *list) {
  if (!list->count) { return ARNM_SUCCESS; }

  for (uint32_t i = 0; i < list->count; ++i) {
    grdw_encrypted_memo *memo = &memos[i];
    memo->memo.data = NULL;
    memo->memo.size = 0;
    arnm_memory_block type_name = {NULL, 0};
    arnm_json_field fields[] = {
        ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_TYPE, &type_name),
        ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_MEMO, &memo->memo)
    };
    uint64_t seen = 0;
    arnm_result result = arnm_json_read_object(list->values[i], fields, 2u, &seen);
    if (ARNM_SUCCESS != result) { return result; }
    if (ALL_SEEN(2) != seen) { return ARNM_ERROR_DECODE_FAILED; }
    memo->type = grdt_memo_key_from_string(chars(&type_name), type_name.size);
    if (GRDT_MEMO_KEY_NONE == memo->type) { return ARNM_ERROR_ENUM_UNKNOWN; }
  }
  return ARNM_SUCCESS;
}

// ********** sizing the transaction's own arena *********************************************
/**
 * @brief Bytes the arrays and the byte blocks of this document will occupy.
 *
 * The counterpart of calculate_memory_size() in the wire mapping, and it answers the same question
 * from the other bank: how much ground the transaction needs before a byte of it is copied.
 * Nothing is searched for -- the root walk found every member this reads and the element handles
 * are already in hand, so this is counting and no more.
 *
 * @param[out] out                Bytes to open the arena with, aligned the way an arena charges.
 * @param[in]  view               What the root walk left.
 * @param[in]  balances           Elements of the balances array.
 * @param[in]  memos              Elements of the memo array.
 * @param[in]  signatures         Elements of the signature array.
 * @param[in]  has_pairing_uuid   Whether a pairing community uuid was read.
 * @param[in]  has_pairing_anchor Whether a pairing ledger anchor was read.
 * @retval ARNM_SUCCESS                    @p out holds the figure.
 * @retval ARNM_ERROR_DECODE_FAILED        A memo or the body carries something that is not base64.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE    A memo's payload is there and is no string.
 * @retval ARNM_ERROR_RESOURCE_SIZE_EXCEED The sum is past what an arena can be opened with.
 */
static arnm_result calculate_memory_size(
    uint32_t *out,
    grdw_encrypted_memo* encrypted_memos,
    const root_view *view,
    const element_list *balances,
    const element_list *memos,
    const element_list *signatures
) {
  uint64_t total = 0;
  total += ARNM_ALIGN8((uint64_t)balances->count * sizeof(grdw_account_balance));
  total += ARNM_ALIGN8((uint64_t)memos->count * sizeof(grdw_encrypted_memo));
  if (memos->count) {
    arnm_result result = read_encrypted_memos(encrypted_memos, memos);
    if (ARNM_SUCCESS != result) { return result; }
    for (uint32_t i = 0; i < memos->count; i++) {
      arnm_memory_block* m = &encrypted_memos[i].memo;
      uint32_t memo_size = 0;
      result = arnm_base64_binary_size((const char*)m->data, m->size, &memo_size);
      if (ARNM_SUCCESS != result) { return result; }
      total += ARNM_ALIGN8((uint64_t)memo_size);
    }
  }
  total += ARNM_ALIGN8((uint64_t)signatures->count * sizeof(grdw_signature_pair));

  if (ARNM_UUID_BINARY_SIZE == view->tx_pairing_community_uuid.size) {
    total += ARNM_ALIGN8((uint64_t)ARNM_UUID_BINARY_SIZE);
  }
  if (view->pairing_ledger_anchor) { total += ARNM_ALIGN8((uint64_t)sizeof(grdw_ledger_anchor)); }

  uint32_t body_size = 0;
  const arnm_result sized =
      arnm_base64_binary_size((const char*)view->body_bytes.data, view->body_bytes.size, &body_size);
  if (ARNM_SUCCESS != sized) { return sized; }
  total += ARNM_ALIGN8((uint64_t)body_size);

  // the counts and the lengths came out of a document, and a document may claim more than a
  // uint32_t holds. Refusing here is what keeps a sum past 4 GiB from wrapping into a small arena
  // that every later allocation then runs past -- the same guard the wire mapping keeps.
  if (total > ARNM_MAX_ALLOC_SIZE) { return ARNM_ERROR_RESOURCE_SIZE_EXCEED; }
  *out = (uint32_t)total;
  return ARNM_SUCCESS;
}

// ********** the arrays and the blocks, once the arena is open ******************************

/**
 * @brief Decode a base64 string of the document into a block of the transaction's arena.
 *
 * The decode runs over the document's own characters, which are this reader's copy of the input
 * and are not looked at again once the bytes they spell are out of them. That is what keeps the
 * conversion off the arena: nothing is written there that a refusal would have to take back, and
 * an empty string leaves the block empty rather than asking for nothing.
 *
 * @param[out]    out   Block to fill; left empty where the string decodes to no bytes at all.
 * @param[in,out] text  The characters, borrowed from the document and spent by this call.
 * @param[in,out] arena The transaction's own memory.
 * @retval ARNM_ERROR_DECODE_FAILED @p text is not base64 this reader accepts.
 */
static arnm_result read_base64_block(arnm_memory_block *out, arnm_memory_block *text, arnm *arena) {
  uint32_t size = 0;
  if (ARNM_SUCCESS != arnm_binary_from_base64_insitu((char *)text->data, text->size, &size)) {
    return ARNM_ERROR_DECODE_FAILED;
  }
  if (!size) { return ARNM_SUCCESS; }
  const arnm_result result = arnm_memory_block_alloc(out, size, arena);
  if (ARNM_SUCCESS != result) { return result; }
  memcpy(out->data, text->data, size);
  return ARNM_SUCCESS;
}

/** @brief Read the balances as they settled. */
static arnm_result read_account_balances(grdr_complete_transaction *tx, const element_list *list) {
  if (!list->count) { return ARNM_SUCCESS; }
  // the cast is what calculate_memory_size() already bounded: the arena could not have been
  // opened if this product did not fit
  arnm_result result = arnm_alloc(
      (uint8_t **)&tx->account_balances, (uint32_t)(list->count * sizeof(grdw_account_balance)),
      &tx->memory_area
  );
  if (ARNM_SUCCESS != result) { return result; }

  for (uint32_t i = 0; i < list->count; ++i) {
    grdw_account_balance *balance = &tx->account_balances[i];
    arnm_memory_block pubkey = ARNM_JSON_BLOCK_OF(balance->pubkey);
    arnm_memory_block community_uuid = ARNM_JSON_BLOCK_OF(balance->community_uuid);
    arnm_json_field fields[] = {
        ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_PUBKEY, &pubkey),
        ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_BALANCE, &balance->balance),
        ARNM_JSON_FIELD_UUID(GRDM_JSON_KEY_COMMUNITY_UUID, &community_uuid)
    };
    uint64_t seen = 0;
    result = arnm_json_read_object(list->values[i], fields, 3u, &seen);
    if (ARNM_SUCCESS != result) { return result; }
    if (ALL_SEEN(3) != seen) { return ARNM_ERROR_DECODE_FAILED; }
    tx->account_balances_count = (size_t)i + 1u;
  }
  return ARNM_SUCCESS;
}

/** @brief Read the memos, each into a block of its own. */
static arnm_result deserialize_encrypted_memos(grdr_complete_transaction *tx, grdw_encrypted_memo *memos, uint32_t memos_count) {
  if (!memos_count) { return ARNM_SUCCESS; }

  tx->encrypted_memos_count = memos_count;
  arnm_result result = arnm_alloc((uint8_t **)&tx->encrypted_memos, sizeof(grdw_encrypted_memo) * memos_count, &tx->memory_area);
  if (ARNM_SUCCESS != result) { return result; }

  for (uint32_t i = 0; i < memos_count; ++i) {
    grdw_encrypted_memo *in = &memos[i];
    grdw_encrypted_memo *out = &tx->encrypted_memos[i];
    out->type = in->type;
    uint32_t memo_buffer_size = 0;
    result = arnm_base64_binary_size((const char*)in->memo.data, in->memo.size, &memo_buffer_size);
    if (ARNM_SUCCESS != result) { return result; }
    result = arnm_memory_block_alloc(&out->memo, memo_buffer_size, &tx->memory_area);
    if (ARNM_SUCCESS != result) { return result; }
    result = arnm_binary_from_base64(out->memo.data, &memo_buffer_size, (const char*)in->memo.data);
    if (ARNM_SUCCESS != result) { return result; }
  }
  return ARNM_SUCCESS;
}

/** @brief Read the signatures over the body bytes. */
static arnm_result read_signature_pairs(grdr_complete_transaction *tx, const element_list *list) {
  if (!list->count) { return ARNM_SUCCESS; }
  arnm_result result = arnm_alloc(
      (uint8_t **)&tx->signature_pairs, (uint32_t)(list->count * sizeof(grdw_signature_pair)),
      &tx->memory_area
  );
  if (ARNM_SUCCESS != result) { return result; }

  for (uint32_t i = 0; i < list->count; ++i) {
    grdw_signature_pair *pair = &tx->signature_pairs[i];
    arnm_memory_block public_key = ARNM_JSON_BLOCK_OF(pair->public_key);
    arnm_memory_block signature = ARNM_JSON_BLOCK_OF(pair->signature);
    arnm_json_field fields[] = {
        ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_PUBLIC_KEY, &public_key),
        ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_SIGNATURE, &signature)
    };
    uint64_t seen = 0;
    result = arnm_json_read_object(list->values[i], fields, 2u, &seen);
    if (ARNM_SUCCESS != result) { return result; }
    if (ALL_SEEN(2) != seen) { return ARNM_ERROR_DECODE_FAILED; }
    tx->signature_pairs_count = (size_t)i + 1u;
  }
  return ARNM_SUCCESS;
}

// ********** the whole transaction **********************************************************

/**
 * @brief Fill the transaction from what the walks found, arena and all.
 *
 * The order is the one the sizing assumed: everything the arena has to hold is measured first,
 * the arena is opened once, and only then is a byte copied into it.
 */
static arnm_result read_complete_transaction(
    grdr_complete_transaction *tx,
    root_view *view,
    grdw_encrypted_memo* encrypted_memos,
    const element_list *balances,
    const element_list *memos,
    const element_list *signatures
) {

  uint32_t memory_size = 0;
  arnm_result result = calculate_memory_size(&memory_size, encrypted_memos, view, balances, memos, signatures);
  if (ARNM_SUCCESS != result) { return result; }
  if (memory_size) {
    result = arnm_init_arena(&tx->memory_area, memory_size);
    if (ARNM_SUCCESS != result) { return result; }
  }

  result = read_timestamp(&tx->confirmed_at, view->confirmed_at);
  if (ARNM_SUCCESS != result) { return result; }
  result = read_timestamp(&tx->created_at, view->created_at);
  if (ARNM_SUCCESS != result) { return result; }
  result = read_ledger_anchor(&tx->ledger_anchor, view->ledger_anchor);
  if (ARNM_SUCCESS != result) { return result; }

  result = read_transaction_detail(tx, view);
  if (ARNM_SUCCESS != result) { return result; }

  result = read_account_balances(tx, balances);
  if (ARNM_SUCCESS != result) { return result; }
  result = deserialize_encrypted_memos(tx, encrypted_memos, memos->count);
  if (ARNM_SUCCESS != result) { return result; }
  result = read_signature_pairs(tx, signatures);
  if (ARNM_SUCCESS != result) { return result; }

  if (ARNM_UUID_BINARY_SIZE == view->tx_pairing_community_uuid.size) {
    result = arnm_alloc(&tx->tx_pairing_community_uuid, ARNM_UUID_BINARY_SIZE, &tx->memory_area);
    if (ARNM_SUCCESS != result) { return result; }
    result = arnm_binary_from_hex_with_known_hex_size(
        tx->tx_pairing_community_uuid, (const char *)view->tx_pairing_community_uuid.data,
        view->tx_pairing_community_uuid.size
    );
    if (ARNM_SUCCESS != result) { return result; }
  }
  if (view->pairing_ledger_anchor) {
    result = arnm_alloc(
        (uint8_t **)&tx->pairing_ledger_anchor, (uint32_t)sizeof(grdw_ledger_anchor),
        &tx->memory_area
    );
    if (ARNM_SUCCESS != result) { return result; }
    result = read_ledger_anchor(tx->pairing_ledger_anchor, view->pairing_ledger_anchor);
    if (ARNM_SUCCESS != result) { return result; }
  }

  return read_base64_block(&tx->body_bytes, &view->body_bytes, &tx->memory_area);
}

#define DEFAULT_MAX_MEMOS_COUNT 4u
/**
 * @brief Read the document from its root: the walk, the enumerations, the arrays, the rest.
 *
 * @param[out]    tx         Transaction to fill; released by the caller on any refusal.
 * @param[in]     root       The document's root; not NULL.
 * @param[in]     node_count Nodes in the document, the bound a widened element buffer is sized by.
 * @param[in,out] allocator  Where a widened element buffer comes from.
 */
static arnm_result read_document(
    grdr_complete_transaction *tx, arnm_json_value *root, uint32_t node_count, arnm *allocator
) {

  // read transaction type first hand, lesser misses as asking for incomplete set of keys
  arnm_memory_block transaction_type_string = {0};
  arnm_json_field transaction_type_field[] = {
      ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_TRANSACTION_TYPE, &transaction_type_string)
  };
  arnm_result result = arnm_json_read_object(root, transaction_type_field, 1, NULL);
  if (ARNM_SUCCESS != result) { return result; }
  if (!transaction_type_string.size) { return ARNM_ERROR_DECODE_FAILED; }
  tx->transaction_type = grdt_transaction_from_string(
      (const char *)transaction_type_string.data, transaction_type_string.size
  );
  if (GRDT_TRANSACTION_NONE == tx->transaction_type) { return ARNM_ERROR_ENUM_UNKNOWN; }

  root_view view = {0};
  result = read_root(&view, tx, root, tx->transaction_type);
  if (ARNM_SUCCESS != result) { return result; }

  // the three enumerations first: the transaction type decides which detail member matters, and a
  // walk cannot promise to have met it before the members it decides for. Each name is turned into
  // a value by the grdt_*_from_string() of the type that owns it, so no enumerator is spelled in
  // this file and there is no second table to fall out of step with the first.

  tx->balance_derivation_type = grdt_balance_derivation_from_string(
      chars(&view.balance_derivation_type), view.balance_derivation_type.size
  );
  if (GRDT_BALANCE_DERIVATION_UNSPECIFIED == tx->balance_derivation_type) {
    return ARNM_ERROR_ENUM_UNKNOWN;
  }
  tx->cross_group_type =
      grdt_cross_group_from_string(chars(&view.cross_group_type), view.cross_group_type.size);
  if (GRDT_CROSS_GROUP_NONE == tx->cross_group_type) { return ARNM_ERROR_ENUM_UNKNOWN; }

  // the arrays are taken as handles before anything else, because the arena is sized from them
  element_list balances;
  element_list memos;
  element_list signatures;
  init_elements(&balances);
  init_elements(&memos);
  init_elements(&signatures);
  grdw_encrypted_memo static_encrypted_memos[DEFAULT_MAX_MEMOS_COUNT];
  grdw_encrypted_memo* encrypted_memos = static_encrypted_memos;

  result = read_elements(&balances, view.account_balances, node_count, allocator);
  if (ARNM_SUCCESS == result) {
    result = read_elements(&memos, view.encrypted_memos, node_count, allocator);
    if (memos.count > DEFAULT_MAX_MEMOS_COUNT) {
      result = arnm_alloc((uint8_t**)&encrypted_memos, sizeof(grdw_encrypted_memo)*memos.count, allocator);
    }
  }
  if (ARNM_SUCCESS == result) {
    result = read_elements(&signatures, view.signature_pairs, node_count, allocator);
  }
  if (ARNM_SUCCESS == result) {
    result = read_complete_transaction(tx, &view, encrypted_memos, &balances, &memos, &signatures);
  }

  // given back in the order they were taken, so an arena gets each one from its own tail
  release_elements(&signatures, allocator);
  if (memos.count > DEFAULT_MAX_MEMOS_COUNT) {
    arnm_free((uint8_t*)encrypted_memos, sizeof(grdw_encrypted_memo)*memos.count, allocator);
  }
  release_elements(&memos, allocator);
  release_elements(&balances, allocator);
  return result;
}

arnm_result grdm_complete_transaction_from_json(
    grdr_complete_transaction *tx, const char *json, uint32_t json_length, arnm *allocator
) {
  if (!tx || !json) { return ARNM_ERROR_NULL_POINTER; }
  if (!json_length) { return ARNM_ERROR_INVALID_PARAM; }

  arnm_json_reader reader;
  arnm_result result = arnm_json_reader_init(&reader, allocator);
  if (ARNM_SUCCESS != result) { return result; }

  arnm_json_value *root = NULL;
  result = arnm_json_reader_parse(&reader, json, json_length, false, &root);
  if (ARNM_SUCCESS == result) {
    grdr_complete_transaction_release(tx);
    result = read_document(tx, root, arnm_json_reader_value_count(&reader), allocator);
    if (ARNM_SUCCESS != result) { grdr_complete_transaction_release(tx); }
  }

  // the document drew from the caller's allocator; an arena that cannot take it back from its
  // tail keeps it until its own reset, which is the caller's rhythm and not this transaction's
  (void)arnm_json_reader_release(&reader);
  return result;
}
