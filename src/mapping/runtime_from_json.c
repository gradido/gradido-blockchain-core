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
  arnm_memory_block address_name = {NULL, 0};
  arnm_json_field fields[] = {
      ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_USER_PUBLIC_KEY, &user_public_key),
      ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_NAME_HASH, &name_hash),
      ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_ACCOUNT_PUBLIC_KEY, &account_public_key),
      // the second union, not the one above: same transaction, other half of the struct
      ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_ADDRESS_TYPE, &address_name),
      ARNM_JSON_FIELD_UINT32(GRDM_JSON_KEY_DERIVATION_INDEX, &tx->derivation_index)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(object, fields, 5u, &seen);
  if (ARNM_SUCCESS != result) { return result; }
  if (ALL_SEEN(5) != seen) { return ARNM_ERROR_DECODE_FAILED; }

  tx->address_type = grdt_address_from_string(chars(&address_name), address_name.size);
  return (GRDT_ADDRESS_NONE == tx->address_type) ? ARNM_ERROR_ENUM_UNKNOWN : ARNM_SUCCESS;
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

/**
 * @brief Read one member of the root, named by a table of exactly one field.
 *
 * The way a scalar the transaction's type owns is read: it cannot be converted from a handle the
 * root walk filed, and only the type knows which of the three is wanted, so the one that is asked
 * for is asked for here. The walk stops at the key it names, and a document that does not carry it
 * is a document missing something its type promised.
 *
 * @param[in]     root  The root object; not NULL.
 * @param[in,out] field A table of one entry, built by an ARNM_JSON_FIELD_* macro.
 * @retval ARNM_ERROR_DECODE_FAILED The member is not there.
 */
static arnm_result read_lone_member(arnm_json_value *root, arnm_json_field *field) {
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(root, field, 1u, &seen);
  if (ARNM_SUCCESS != result) { return result; }
  return (ALL_SEEN(1) == seen) ? ARNM_SUCCESS : ARNM_ERROR_DECODE_FAILED;
}

// ********** the root, walked once **********************************************************

/**
 * @brief The members of the root object this mapping wants, in the order a document writes them.
 *
 * The order is what the walk is paid in: it starts each key at the lowest entry it has not filled
 * yet, so a table in the document's own order is met one comparison per member. The three context
 * scalars are not here on purpose -- see @ref read_lone_member().
 */
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
  ROOT_FIELD_TRANSFER,
  ROOT_FIELD_REGISTER_ADDRESS,
  ROOT_FIELD_COMMUNITY_ROOT,
  ROOT_FIELD_ACCOUNT_BALANCES,
  ROOT_FIELD_ENCRYPTED_MEMOS,
  ROOT_FIELD_SIGNATURE_PAIRS,
  ROOT_FIELD_TX_PAIRING_COMMUNITY_UUID,
  ROOT_FIELD_PAIRING_LEDGER_ANCHOR,
  ROOT_FIELD_BODY_BYTES,
  ROOT_FIELD_COUNT
} root_field;

/**
 * @brief What every document carries, whatever its transaction type.
 *
 * The envelope through the running hash, and the body bytes at the end. What a type owns is
 * required too, but which member that is only the type says, so those are asked for where the
 * branch is taken rather than counted here.
 */
#define ROOT_REQUIRED (ALL_SEEN(ROOT_FIELD_TRANSFER) | SEEN(ROOT_FIELD_BODY_BYTES))

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
  arnm_json_value *tx_pairing_community_uuid;
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
    root_view *view, grdr_complete_transaction *tx, arnm_json_value *root
) {
  memset(view, 0, sizeof(*view));
  view->root = root;

  arnm_memory_block tx_community_uuid = ARNM_JSON_BLOCK_OF(tx->tx_community_uuid);
  arnm_memory_block tx_running_hash = ARNM_JSON_BLOCK_OF(tx->tx_running_hash);
  arnm_json_field fields[] = {
      ARNM_JSON_FIELD_UINT64(GRDM_JSON_KEY_TX_NR, &tx->tx_nr),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_CONFIRMED_AT, &view->confirmed_at),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_CREATED_AT, &view->created_at),
      ARNM_JSON_FIELD_UUID(GRDM_JSON_KEY_TX_COMMUNITY_UUID, &tx_community_uuid),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_LEDGER_ANCHOR, &view->ledger_anchor),
      ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_TRANSACTION_TYPE, &view->transaction_type),
      ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_BALANCE_DERIVATION_TYPE, &view->balance_derivation_type),
      ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_CROSS_GROUP_TYPE, &view->cross_group_type),
      ARNM_JSON_FIELD_HEX_FIXED(GRDM_JSON_KEY_TX_RUNNING_HASH, &tx_running_hash),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_TRANSFER, &view->transfer),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_REGISTER_ADDRESS, &view->register_address),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_COMMUNITY_ROOT, &view->community_root),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_ACCOUNT_BALANCES, &view->account_balances),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_ENCRYPTED_MEMOS, &view->encrypted_memos),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_SIGNATURE_PAIRS, &view->signature_pairs),
      ARNM_JSON_FIELD_VALUE(
          GRDM_JSON_KEY_TX_PAIRING_COMMUNITY_UUID, &view->tx_pairing_community_uuid
      ),
      ARNM_JSON_FIELD_VALUE(GRDM_JSON_KEY_PAIRING_LEDGER_ANCHOR, &view->pairing_ledger_anchor),
      ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_BODY_BYTES, &view->body_bytes)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(root, fields, ROOT_FIELD_COUNT, &seen);
  if (ARNM_SUCCESS != result) { return result; }
  return (ROOT_REQUIRED == (seen & ROOT_REQUIRED)) ? ARNM_SUCCESS : ARNM_ERROR_DECODE_FAILED;
}

/**
 * @brief Read the detail and the context the transaction's type owns, and nothing else.
 *
 * Sorted by expected frequency of occurrence, the same order the wire mapping keeps. A type with
 * no layout here is refused rather than half read -- the same refusal
 * grdm_complete_transaction_from_wire() answers for the same types.
 */
static arnm_result read_transaction_detail(grdr_complete_transaction *tx, const root_view *view) {
  arnm_result result = ARNM_SUCCESS;
  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_TRANSFER:
    return read_transfer(tx, view->transfer);
  case GRDT_TRANSACTION_CREATION: {
    result = read_transfer(tx, view->transfer);
    if (ARNM_SUCCESS != result) { return result; }
    arnm_json_field target_date[] = {
        ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_TARGET_DATE, &tx->target_date)
    };
    return read_lone_member(view->root, target_date);
  }
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    return read_register_address(tx, view->register_address);
  case GRDT_TRANSACTION_DEFERRED_TRANSFER: {
    result = read_transfer(tx, view->transfer);
    if (ARNM_SUCCESS != result) { return result; }
    arnm_json_field timeout_duration[] = {
        ARNM_JSON_FIELD_INT64(GRDM_JSON_KEY_TIMEOUT_DURATION, &tx->timeout_duration)
    };
    return read_lone_member(view->root, timeout_duration);
  }
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER: {
    result = read_transfer(tx, view->transfer);
    if (ARNM_SUCCESS != result) { return result; }
    arnm_json_field previous_tx[] = {
        ARNM_JSON_FIELD_UINT64(GRDM_JSON_KEY_PREVIOUS_TX, &tx->previous_tx)
    };
    return read_lone_member(view->root, previous_tx);
  }
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER: {
    arnm_json_field previous_tx[] = {
        ARNM_JSON_FIELD_UINT64(GRDM_JSON_KEY_PREVIOUS_TX, &tx->previous_tx)
    };
    return read_lone_member(view->root, previous_tx);
  }
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    return read_community_root(tx, view->community_root);
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
#define ELEMENTS_INLINE 16u

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

// ********** sizing the transaction's own arena *********************************************

/**
 * @brief Bytes the memo payloads of @p memos will occupy, aligned the way an arena charges.
 *
 * The one array whose elements have to be looked at before the arena exists: a balance and a
 * signature are as long as their types say, a memo is as long as its own base64. Which is why this
 * is the only array walked twice -- once here for its lengths, once below for its bytes.
 */
static arnm_result memo_payload_size(uint64_t *total, const element_list *memos) {
  for (uint32_t i = 0; i < memos->count; ++i) {
    // only the payload is wanted here; the type this element also carries is read on the second
    // walk, where the bytes it names actually go
    arnm_memory_block text = {NULL, 0};
    arnm_json_field fields[] = {ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_MEMO, &text)};
    uint64_t seen = 0;
    const arnm_result walked = arnm_json_read_object(memos->values[i], fields, 1u, &seen);
    if (ARNM_SUCCESS != walked) { return walked; }
    if (ALL_SEEN(1) != seen) { return ARNM_ERROR_DECODE_FAILED; }

    uint32_t size = 0;
    const arnm_result sized = arnm_base64_binary_size(chars(&text), text.size, &size);
    if (ARNM_SUCCESS != sized) { return sized; }
    *total += ARNM_ALIGN8((uint64_t)size);
  }
  return ARNM_SUCCESS;
}

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
    const root_view *view,
    const element_list *balances,
    const element_list *memos,
    const element_list *signatures,
    bool has_pairing_uuid,
    bool has_pairing_anchor
) {
  uint64_t total = 0;
  total += ARNM_ALIGN8((uint64_t)balances->count * sizeof(grdw_account_balance));
  total += ARNM_ALIGN8((uint64_t)memos->count * sizeof(grdw_encrypted_memo));
  if (memos->count) {
    const arnm_result result = memo_payload_size(&total, memos);
    if (ARNM_SUCCESS != result) { return result; }
  }
  total += ARNM_ALIGN8((uint64_t)signatures->count * sizeof(grdw_signature_pair));

  if (has_pairing_uuid) { total += ARNM_ALIGN8((uint64_t)ARNM_UUID_BINARY_SIZE); }
  if (has_pairing_anchor) { total += ARNM_ALIGN8((uint64_t)sizeof(grdw_ledger_anchor)); }

  uint32_t body_size = 0;
  const arnm_result sized =
      arnm_base64_binary_size(chars(&view->body_bytes), view->body_bytes.size, &body_size);
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
static arnm_result read_encrypted_memos(grdr_complete_transaction *tx, const element_list *list) {
  if (!list->count) { return ARNM_SUCCESS; }
  arnm_result result = arnm_alloc(
      (uint8_t **)&tx->encrypted_memos, (uint32_t)(list->count * sizeof(grdw_encrypted_memo)),
      &tx->memory_area
  );
  if (ARNM_SUCCESS != result) { return result; }

  for (uint32_t i = 0; i < list->count; ++i) {
    grdw_encrypted_memo *memo = &tx->encrypted_memos[i];
    memo->memo.data = NULL;
    memo->memo.size = 0;
    arnm_memory_block type_name = {NULL, 0};
    arnm_memory_block text = {NULL, 0};
    arnm_json_field fields[] = {
        ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_TYPE, &type_name),
        ARNM_JSON_FIELD_STRING(GRDM_JSON_KEY_MEMO, &text)
    };
    uint64_t seen = 0;
    result = arnm_json_read_object(list->values[i], fields, 2u, &seen);
    if (ARNM_SUCCESS != result) { return result; }
    if (ALL_SEEN(2) != seen) { return ARNM_ERROR_DECODE_FAILED; }

    memo->type = grdt_memo_key_from_string(chars(&type_name), type_name.size);
    if (GRDT_MEMO_KEY_NONE == memo->type) { return ARNM_ERROR_ENUM_UNKNOWN; }
    result = read_base64_block(&memo->memo, &text, &tx->memory_area);
    if (ARNM_SUCCESS != result) { return result; }
    tx->encrypted_memos_count = (size_t)i + 1u;
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

// ********** the two members only a transaction that is not local carries *******************

/**
 * @brief Read the pairing community uuid, in a walk of the root of its own.
 *
 * A uuid is a string, and a string can only be decoded where a table names it -- so the member the
 * root walk filed as a handle is asked for a second time, and only where that walk found it. A
 * local transaction never carries it and never pays for this.
 *
 * @param[out] uuid    The sixteen bytes; written only where @p present comes back true.
 * @param[out] present Whether the document carried a uuid here.
 * @param[in]  root    The root object; not NULL.
 * @retval ARNM_ERROR_DECODE_FAILED The member is a string and is not the canonical 36 characters.
 */
static arnm_result read_pairing_uuid(
    uint8_t uuid[ARNM_UUID_BINARY_SIZE], bool *present, arnm_json_value *root
) {
  *present = false;
  arnm_memory_block block = {uuid, (uint32_t)ARNM_UUID_BINARY_SIZE};
  arnm_json_field fields[] = {
      ARNM_JSON_FIELD_UUID(GRDM_JSON_KEY_TX_PAIRING_COMMUNITY_UUID, &block)
  };
  uint64_t seen = 0;
  const arnm_result result = arnm_json_read_object(root, fields, 1u, &seen);
  // an optional member that is no string carries nothing to read; `null` is the case that matters
  if (ARNM_ERROR_INVALID_ENUM_TYPE == result) { return ARNM_SUCCESS; }
  if (ARNM_SUCCESS != result) { return result; }
  *present = (ALL_SEEN(1) == seen);
  return ARNM_SUCCESS;
}

/**
 * @brief Read the pairing anchor into storage of the caller's, before any arena exists.
 *
 * @param[out] anchor  The anchor; written only where @p present comes back true.
 * @param[out] present Whether the document carried an anchor here.
 * @param[in]  value   The member as the root walk filed it, or NULL where it was absent.
 */
static arnm_result read_pairing_anchor(
    grdw_ledger_anchor *anchor, bool *present, arnm_json_value *value
) {
  *present = false;
  if (!value) { return ARNM_SUCCESS; }
  const arnm_result result = read_ledger_anchor(anchor, value);
  // no object here is no anchor here, which for an optional member is nothing to read. An object
  // that is one and is wrong about itself stays a refusal.
  if (ARNM_ERROR_INVALID_ENUM_TYPE == result) { return ARNM_SUCCESS; }
  if (ARNM_SUCCESS != result) { return result; }
  *present = true;
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
    const element_list *balances,
    const element_list *memos,
    const element_list *signatures
) {
  // the two pairing members go into storage of this frame's first, because the arena they end up
  // in cannot be opened before it is known whether they are there at all
  uint8_t pairing_uuid[ARNM_UUID_BINARY_SIZE];
  bool has_pairing_uuid = false;
  if (view->tx_pairing_community_uuid) {
    const arnm_result result = read_pairing_uuid(pairing_uuid, &has_pairing_uuid, view->root);
    if (ARNM_SUCCESS != result) { return result; }
  }
  grdw_ledger_anchor pairing_anchor;
  bool has_pairing_anchor = false;
  {
    const arnm_result result =
        read_pairing_anchor(&pairing_anchor, &has_pairing_anchor, view->pairing_ledger_anchor);
    if (ARNM_SUCCESS != result) { return result; }
  }

  uint32_t memory_size = 0;
  arnm_result result = calculate_memory_size(
      &memory_size, view, balances, memos, signatures, has_pairing_uuid, has_pairing_anchor
  );
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
  result = read_encrypted_memos(tx, memos);
  if (ARNM_SUCCESS != result) { return result; }
  result = read_signature_pairs(tx, signatures);
  if (ARNM_SUCCESS != result) { return result; }

  if (has_pairing_uuid) {
    result = arnm_alloc(&tx->tx_pairing_community_uuid, ARNM_UUID_BINARY_SIZE, &tx->memory_area);
    if (ARNM_SUCCESS != result) { return result; }
    memcpy(tx->tx_pairing_community_uuid, pairing_uuid, ARNM_UUID_BINARY_SIZE);
  }
  if (has_pairing_anchor) {
    result = arnm_alloc(
        (uint8_t **)&tx->pairing_ledger_anchor, (uint32_t)sizeof(grdw_ledger_anchor),
        &tx->memory_area
    );
    if (ARNM_SUCCESS != result) { return result; }
    *tx->pairing_ledger_anchor = pairing_anchor;
  }

  return read_base64_block(&tx->body_bytes, &view->body_bytes, &tx->memory_area);
}

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
  root_view view;
  arnm_result result = read_root(&view, tx, root);
  if (ARNM_SUCCESS != result) { return result; }

  // the three enumerations first: the transaction type decides which detail member matters, and a
  // walk cannot promise to have met it before the members it decides for. Each name is turned into
  // a value by the grdt_*_from_string() of the type that owns it, so no enumerator is spelled in
  // this file and there is no second table to fall out of step with the first.
  tx->transaction_type =
      grdt_transaction_from_string(chars(&view.transaction_type), view.transaction_type.size);
  if (GRDT_TRANSACTION_NONE == tx->transaction_type) { return ARNM_ERROR_ENUM_UNKNOWN; }
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

  result = read_elements(&balances, view.account_balances, node_count, allocator);
  if (ARNM_SUCCESS == result) {
    result = read_elements(&memos, view.encrypted_memos, node_count, allocator);
  }
  if (ARNM_SUCCESS == result) {
    result = read_elements(&signatures, view.signature_pairs, node_count, allocator);
  }
  if (ARNM_SUCCESS == result) {
    result = read_complete_transaction(tx, &view, &balances, &memos, &signatures);
  }

  // given back in the order they were taken, so an arena gets each one from its own tail
  release_elements(&signatures, allocator);
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
