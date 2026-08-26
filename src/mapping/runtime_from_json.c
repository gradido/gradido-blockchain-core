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
 * So nothing here asks. Every object is walked once, every key is handed to a recogniser that
 * answers what it is with a switch on its length and at most one memcmp, and the value is filed
 * in a slot the recogniser named. What comes out is a small array of pointers into the
 * document, indexed by field: present members hold their value, absent ones hold NULL, and
 * reading the object afterwards is array indexing with no searching left in it.
 *
 * The second thing that falls out of the walk is order. A JSON object promises none, and this
 * mapping needs `transaction_type` before it can know whether `transfer` or `community_root` is
 * the member that matters -- which a walk cannot guarantee having reached. Collecting first and
 * deciding afterwards makes the question disappear rather than answering it.
 */

// ********** one walk per object ***********************************************************

/**
 * @brief What a recogniser answers: the field a key names, or 0 for one this shape ignores.
 *
 * Every shape below has an enumeration of its own whose first member is its "none", so one
 * function type serves all of them and @ref collect_members() needs no variant per object.
 */
typedef uint32_t (*field_recogniser)(const char *key, uint32_t key_size);

/**
 * @brief Walk @p object once and file every member it carries under the field its key names.
 *
 * @param[in]  object    Object to walk; not NULL.
 * @param[out] member    Slots to fill, one per field; every one is written, none is read, so
 *                       uninitialised storage is a valid input. A field the object does not
 *                       carry stays NULL.
 * @param[in]  count     Slots in @p member -- the shape's `_COUNT`.
 * @param[in]  recognise What turns a key into one of those fields.
 * @retval ARNM_SUCCESS                 Walked; @p member holds what the object had.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE @p object is not an object.
 * @note A duplicate key keeps the last of its values, which is what a second assignment to the
 *       same slot does and what JSON leaves open anyway.
 * @whisper Every name asked once what it is, instead of every question asked of every name
 */
static arnm_result collect_members(
    const arnm_json_value *object,
    arnm_json_value **member,
    uint32_t count,
    field_recogniser recognise
) {
  for (uint32_t i = 0; i < count; ++i) { member[i] = NULL; }

  arnm_json_object_iter iter;
  if (ARNM_SUCCESS != arnm_json_object_iter_init(object, &iter)) {
    return ARNM_ERROR_INVALID_ENUM_TYPE;
  }

  const char *key = NULL;
  uint32_t key_size = 0;
  arnm_json_value *value = NULL;
  while (arnm_json_object_iter_next(&iter, &key, &key_size, &value)) {
    const uint32_t field = recognise(key, key_size);
    if (field && field < count) { member[field] = value; }
  }
  return ARNM_SUCCESS;
}

/**
 * @brief The length of a key macro's literal, terminator excluded, worked out by the compiler.
 *
 * Every `case` label of every recogniser below is one of these. A length typed out by hand is
 * a length that can be wrong, and a wrong one is invisible: the key simply never matches, the
 * member reads as absent, and the document is refused for a reason that names the wrong thing.
 * Taken from the literal instead, it cannot drift from the key it belongs to -- and two keys of
 * equal length become a duplicate `case`, which is a compile error at exactly the place that
 * then needs a second look at the first byte.
 */
#define KEY_LEN(key) ((uint32_t)(sizeof(key) - 1u))

/**
 * @brief Whether @p key is exactly the literal @p want, its length already matched by the switch.
 *
 * The length comes from the same literal, so this compares what it means to compare and no
 * terminator with it.
 */
#define KEY_IS(key, want) (0 == memcmp((key), (want), sizeof(want) - 1u))

// ********** the shapes, and the keys each of them knows ************************************

/** @brief Members of the document's root object. */
typedef enum root_field {
  ROOT_FIELD_NONE = 0,
  ROOT_FIELD_TX_NR,
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

/**
 * @brief Recognise a member of the root object.
 *
 * Sorted by the one thing that separates most of these keys for free -- how long they are.
 * Where a length is shared, the first byte settles it, and only then is the whole name
 * compared. Twenty-odd questions of the object become one pass over it.
 */
static uint32_t root_field_of(const char *key, uint32_t key_size) {
  switch (key_size) {
  case KEY_LEN(GRDM_JSON_KEY_TX_NR):
    return KEY_IS(key, GRDM_JSON_KEY_TX_NR) ? ROOT_FIELD_TX_NR : ROOT_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_TRANSFER):
    return KEY_IS(key, GRDM_JSON_KEY_TRANSFER) ? ROOT_FIELD_TRANSFER : ROOT_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_CREATED_AT):
    switch (key[0]) {
    case 'b':
      return KEY_IS(key, GRDM_JSON_KEY_BODY_BYTES) ? ROOT_FIELD_BODY_BYTES : ROOT_FIELD_NONE;
    case 'c':
      return KEY_IS(key, GRDM_JSON_KEY_CREATED_AT) ? ROOT_FIELD_CREATED_AT : ROOT_FIELD_NONE;
    default:
      return ROOT_FIELD_NONE;
    }
  case KEY_LEN(GRDM_JSON_KEY_TARGET_DATE):
    switch (key[0]) {
    case 'p':
      return KEY_IS(key, GRDM_JSON_KEY_PREVIOUS_TX) ? ROOT_FIELD_PREVIOUS_TX : ROOT_FIELD_NONE;
    case 't':
      return KEY_IS(key, GRDM_JSON_KEY_TARGET_DATE) ? ROOT_FIELD_TARGET_DATE : ROOT_FIELD_NONE;
    default:
      return ROOT_FIELD_NONE;
    }
  case KEY_LEN(GRDM_JSON_KEY_CONFIRMED_AT):
    return KEY_IS(key, GRDM_JSON_KEY_CONFIRMED_AT) ? ROOT_FIELD_CONFIRMED_AT : ROOT_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_LEDGER_ANCHOR):
    return KEY_IS(key, GRDM_JSON_KEY_LEDGER_ANCHOR) ? ROOT_FIELD_LEDGER_ANCHOR : ROOT_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_COMMUNITY_ROOT):
    return KEY_IS(key, GRDM_JSON_KEY_COMMUNITY_ROOT) ? ROOT_FIELD_COMMUNITY_ROOT : ROOT_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_TX_RUNNING_HASH):
    switch (key[0]) {
    case 'e':
      return KEY_IS(key, GRDM_JSON_KEY_ENCRYPTED_MEMOS) ? ROOT_FIELD_ENCRYPTED_MEMOS
                                                        : ROOT_FIELD_NONE;
    case 's':
      return KEY_IS(key, GRDM_JSON_KEY_SIGNATURE_PAIRS) ? ROOT_FIELD_SIGNATURE_PAIRS
                                                        : ROOT_FIELD_NONE;
    case 't':
      return KEY_IS(key, GRDM_JSON_KEY_TX_RUNNING_HASH) ? ROOT_FIELD_TX_RUNNING_HASH
                                                        : ROOT_FIELD_NONE;
    default:
      return ROOT_FIELD_NONE;
    }
  case KEY_LEN(GRDM_JSON_KEY_TRANSACTION_TYPE):
    switch (key[0]) {
    case 'a':
      return KEY_IS(key, GRDM_JSON_KEY_ACCOUNT_BALANCES) ? ROOT_FIELD_ACCOUNT_BALANCES
                                                         : ROOT_FIELD_NONE;
    case 'c':
      return KEY_IS(key, GRDM_JSON_KEY_CROSS_GROUP_TYPE) ? ROOT_FIELD_CROSS_GROUP_TYPE
                                                         : ROOT_FIELD_NONE;
    case 'r':
      return KEY_IS(key, GRDM_JSON_KEY_REGISTER_ADDRESS) ? ROOT_FIELD_REGISTER_ADDRESS
                                                         : ROOT_FIELD_NONE;
    case 't':
      if (KEY_IS(key, GRDM_JSON_KEY_TRANSACTION_TYPE)) { return ROOT_FIELD_TRANSACTION_TYPE; }
      return KEY_IS(key, GRDM_JSON_KEY_TIMEOUT_DURATION) ? ROOT_FIELD_TIMEOUT_DURATION
                                                         : ROOT_FIELD_NONE;
    default:
      return ROOT_FIELD_NONE;
    }
  case KEY_LEN(GRDM_JSON_KEY_TX_COMMUNITY_UUID):
    return KEY_IS(key, GRDM_JSON_KEY_TX_COMMUNITY_UUID) ? ROOT_FIELD_TX_COMMUNITY_UUID
                                                        : ROOT_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_PAIRING_LEDGER_ANCHOR):
    return KEY_IS(key, GRDM_JSON_KEY_PAIRING_LEDGER_ANCHOR) ? ROOT_FIELD_PAIRING_LEDGER_ANCHOR
                                                            : ROOT_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_BALANCE_DERIVATION_TYPE):
    return KEY_IS(key, GRDM_JSON_KEY_BALANCE_DERIVATION_TYPE) ? ROOT_FIELD_BALANCE_DERIVATION_TYPE
                                                              : ROOT_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_TX_PAIRING_COMMUNITY_UUID):
    return KEY_IS(key, GRDM_JSON_KEY_TX_PAIRING_COMMUNITY_UUID)
               ? ROOT_FIELD_TX_PAIRING_COMMUNITY_UUID
               : ROOT_FIELD_NONE;
  default:
    return ROOT_FIELD_NONE;
  }
}

/** @brief Members of a timestamp object. */
typedef enum timestamp_field {
  TIMESTAMP_FIELD_NONE = 0,
  TIMESTAMP_FIELD_SECONDS,
  TIMESTAMP_FIELD_NANOS,
  TIMESTAMP_FIELD_COUNT
} timestamp_field;

static uint32_t timestamp_field_of(const char *key, uint32_t key_size) {
  switch (key_size) {
  case KEY_LEN(GRDM_JSON_KEY_NANOS):
    return KEY_IS(key, GRDM_JSON_KEY_NANOS) ? TIMESTAMP_FIELD_NANOS : TIMESTAMP_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_SECONDS):
    return KEY_IS(key, GRDM_JSON_KEY_SECONDS) ? TIMESTAMP_FIELD_SECONDS : TIMESTAMP_FIELD_NONE;
  default:
    return TIMESTAMP_FIELD_NONE;
  }
}

/** @brief Members of a ledger anchor object. */
typedef enum anchor_field {
  ANCHOR_FIELD_NONE = 0,
  ANCHOR_FIELD_TYPE,
  ANCHOR_FIELD_ID,
  ANCHOR_FIELD_HIERO_TRANSACTION_ID,
  ANCHOR_FIELD_COUNT
} anchor_field;

static uint32_t anchor_field_of(const char *key, uint32_t key_size) {
  switch (key_size) {
  case KEY_LEN(GRDM_JSON_KEY_ID):
    return KEY_IS(key, GRDM_JSON_KEY_ID) ? ANCHOR_FIELD_ID : ANCHOR_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_TYPE):
    return KEY_IS(key, GRDM_JSON_KEY_TYPE) ? ANCHOR_FIELD_TYPE : ANCHOR_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_HIERO_TRANSACTION_ID):
    return KEY_IS(key, GRDM_JSON_KEY_HIERO_TRANSACTION_ID) ? ANCHOR_FIELD_HIERO_TRANSACTION_ID
                                                           : ANCHOR_FIELD_NONE;
  default:
    return ANCHOR_FIELD_NONE;
  }
}

/** @brief Members of a hiero transaction id object. */
typedef enum hiero_field {
  HIERO_FIELD_NONE = 0,
  HIERO_FIELD_TRANSACTION_VALID_START,
  HIERO_FIELD_ACCOUNT_ID,
  HIERO_FIELD_COUNT
} hiero_field;

static uint32_t hiero_field_of(const char *key, uint32_t key_size) {
  switch (key_size) {
  case KEY_LEN(GRDM_JSON_KEY_ACCOUNT_ID):
    return KEY_IS(key, GRDM_JSON_KEY_ACCOUNT_ID) ? HIERO_FIELD_ACCOUNT_ID : HIERO_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_TRANSACTION_VALID_START):
    return KEY_IS(key, GRDM_JSON_KEY_TRANSACTION_VALID_START) ? HIERO_FIELD_TRANSACTION_VALID_START
                                                              : HIERO_FIELD_NONE;
  default:
    return HIERO_FIELD_NONE;
  }
}

/** @brief Members of a hiero account id object. */
typedef enum account_id_field {
  ACCOUNT_ID_FIELD_NONE = 0,
  ACCOUNT_ID_FIELD_SHARD_NUM,
  ACCOUNT_ID_FIELD_REALM_NUM,
  ACCOUNT_ID_FIELD_ACCOUNT_NUM,
  ACCOUNT_ID_FIELD_COUNT
} account_id_field;

static uint32_t account_id_field_of(const char *key, uint32_t key_size) {
  switch (key_size) {
  case KEY_LEN(GRDM_JSON_KEY_SHARD_NUM):
    switch (key[0]) {
    case 'r':
      return KEY_IS(key, GRDM_JSON_KEY_REALM_NUM) ? ACCOUNT_ID_FIELD_REALM_NUM
                                                  : ACCOUNT_ID_FIELD_NONE;
    case 's':
      return KEY_IS(key, GRDM_JSON_KEY_SHARD_NUM) ? ACCOUNT_ID_FIELD_SHARD_NUM
                                                  : ACCOUNT_ID_FIELD_NONE;
    default:
      return ACCOUNT_ID_FIELD_NONE;
    }
  case KEY_LEN(GRDM_JSON_KEY_ACCOUNT_NUM):
    return KEY_IS(key, GRDM_JSON_KEY_ACCOUNT_NUM) ? ACCOUNT_ID_FIELD_ACCOUNT_NUM
                                                  : ACCOUNT_ID_FIELD_NONE;
  default:
    return ACCOUNT_ID_FIELD_NONE;
  }
}

/** @brief Members of the transfer detail object. */
typedef enum transfer_field {
  TRANSFER_FIELD_NONE = 0,
  TRANSFER_FIELD_SENDER_PUBKEY,
  TRANSFER_FIELD_RECIPIENT_PUBKEY,
  TRANSFER_FIELD_AMOUNT,
  TRANSFER_FIELD_COIN_COMMUNITY_UUID,
  TRANSFER_FIELD_COUNT
} transfer_field;

static uint32_t transfer_field_of(const char *key, uint32_t key_size) {
  switch (key_size) {
  case KEY_LEN(GRDM_JSON_KEY_AMOUNT):
    return KEY_IS(key, GRDM_JSON_KEY_AMOUNT) ? TRANSFER_FIELD_AMOUNT : TRANSFER_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_SENDER_PUBKEY):
    return KEY_IS(key, GRDM_JSON_KEY_SENDER_PUBKEY) ? TRANSFER_FIELD_SENDER_PUBKEY
                                                    : TRANSFER_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_RECIPIENT_PUBKEY):
    return KEY_IS(key, GRDM_JSON_KEY_RECIPIENT_PUBKEY) ? TRANSFER_FIELD_RECIPIENT_PUBKEY
                                                       : TRANSFER_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_COIN_COMMUNITY_UUID):
    return KEY_IS(key, GRDM_JSON_KEY_COIN_COMMUNITY_UUID) ? TRANSFER_FIELD_COIN_COMMUNITY_UUID
                                                          : TRANSFER_FIELD_NONE;
  default:
    return TRANSFER_FIELD_NONE;
  }
}

/** @brief Members of the register address detail object. */
typedef enum register_address_field {
  REGISTER_ADDRESS_FIELD_NONE = 0,
  REGISTER_ADDRESS_FIELD_USER_PUBLIC_KEY,
  REGISTER_ADDRESS_FIELD_NAME_HASH,
  REGISTER_ADDRESS_FIELD_ACCOUNT_PUBLIC_KEY,
  REGISTER_ADDRESS_FIELD_ADDRESS_TYPE,
  REGISTER_ADDRESS_FIELD_DERIVATION_INDEX,
  REGISTER_ADDRESS_FIELD_COUNT
} register_address_field;

static uint32_t register_address_field_of(const char *key, uint32_t key_size) {
  switch (key_size) {
  case KEY_LEN(GRDM_JSON_KEY_NAME_HASH):
    return KEY_IS(key, GRDM_JSON_KEY_NAME_HASH) ? REGISTER_ADDRESS_FIELD_NAME_HASH
                                                : REGISTER_ADDRESS_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_ADDRESS_TYPE):
    return KEY_IS(key, GRDM_JSON_KEY_ADDRESS_TYPE) ? REGISTER_ADDRESS_FIELD_ADDRESS_TYPE
                                                   : REGISTER_ADDRESS_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_USER_PUBLIC_KEY):
    return KEY_IS(key, GRDM_JSON_KEY_USER_PUBLIC_KEY) ? REGISTER_ADDRESS_FIELD_USER_PUBLIC_KEY
                                                      : REGISTER_ADDRESS_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_DERIVATION_INDEX):
    return KEY_IS(key, GRDM_JSON_KEY_DERIVATION_INDEX) ? REGISTER_ADDRESS_FIELD_DERIVATION_INDEX
                                                       : REGISTER_ADDRESS_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_ACCOUNT_PUBLIC_KEY):
    return KEY_IS(key, GRDM_JSON_KEY_ACCOUNT_PUBLIC_KEY) ? REGISTER_ADDRESS_FIELD_ACCOUNT_PUBLIC_KEY
                                                         : REGISTER_ADDRESS_FIELD_NONE;
  default:
    return REGISTER_ADDRESS_FIELD_NONE;
  }
}

/** @brief Members of the community root detail object. */
typedef enum community_root_field {
  COMMUNITY_ROOT_FIELD_NONE = 0,
  COMMUNITY_ROOT_FIELD_PUBLIC_KEY,
  COMMUNITY_ROOT_FIELD_GMW_PUBLIC_KEY,
  COMMUNITY_ROOT_FIELD_AUF_PUBLIC_KEY,
  COMMUNITY_ROOT_FIELD_COUNT
} community_root_field;

static uint32_t community_root_field_of(const char *key, uint32_t key_size) {
  switch (key_size) {
  case KEY_LEN(GRDM_JSON_KEY_PUBLIC_KEY):
    return KEY_IS(key, GRDM_JSON_KEY_PUBLIC_KEY) ? COMMUNITY_ROOT_FIELD_PUBLIC_KEY
                                                 : COMMUNITY_ROOT_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_GMW_PUBLIC_KEY):
    switch (key[0]) {
    case 'a':
      return KEY_IS(key, GRDM_JSON_KEY_AUF_PUBLIC_KEY) ? COMMUNITY_ROOT_FIELD_AUF_PUBLIC_KEY
                                                       : COMMUNITY_ROOT_FIELD_NONE;
    case 'g':
      return KEY_IS(key, GRDM_JSON_KEY_GMW_PUBLIC_KEY) ? COMMUNITY_ROOT_FIELD_GMW_PUBLIC_KEY
                                                       : COMMUNITY_ROOT_FIELD_NONE;
    default:
      return COMMUNITY_ROOT_FIELD_NONE;
    }
  default:
    return COMMUNITY_ROOT_FIELD_NONE;
  }
}

/** @brief Members of one account balance object. */
typedef enum balance_field {
  BALANCE_FIELD_NONE = 0,
  BALANCE_FIELD_PUBKEY,
  BALANCE_FIELD_BALANCE,
  BALANCE_FIELD_COMMUNITY_UUID,
  BALANCE_FIELD_COUNT
} balance_field;

static uint32_t balance_field_of(const char *key, uint32_t key_size) {
  switch (key_size) {
  case KEY_LEN(GRDM_JSON_KEY_PUBKEY):
    return KEY_IS(key, GRDM_JSON_KEY_PUBKEY) ? BALANCE_FIELD_PUBKEY : BALANCE_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_BALANCE):
    return KEY_IS(key, GRDM_JSON_KEY_BALANCE) ? BALANCE_FIELD_BALANCE : BALANCE_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_COMMUNITY_UUID):
    return KEY_IS(key, GRDM_JSON_KEY_COMMUNITY_UUID) ? BALANCE_FIELD_COMMUNITY_UUID
                                                     : BALANCE_FIELD_NONE;
  default:
    return BALANCE_FIELD_NONE;
  }
}

/** @brief Members of one encrypted memo object. */
typedef enum memo_field {
  MEMO_FIELD_NONE = 0,
  MEMO_FIELD_TYPE,
  MEMO_FIELD_MEMO,
  MEMO_FIELD_COUNT
} memo_field;

static uint32_t memo_field_of(const char *key, uint32_t key_size) {
  switch (key_size) {
  case KEY_LEN(GRDM_JSON_KEY_TYPE):
    switch (key[0]) {
    case 'm':
      return KEY_IS(key, GRDM_JSON_KEY_MEMO) ? MEMO_FIELD_MEMO : MEMO_FIELD_NONE;
    case 't':
      return KEY_IS(key, GRDM_JSON_KEY_TYPE) ? MEMO_FIELD_TYPE : MEMO_FIELD_NONE;
    default:
      return MEMO_FIELD_NONE;
    }
  default:
    return MEMO_FIELD_NONE;
  }
}

/** @brief Members of one signature pair object. */
typedef enum signature_field {
  SIGNATURE_FIELD_NONE = 0,
  SIGNATURE_FIELD_PUBLIC_KEY,
  SIGNATURE_FIELD_SIGNATURE,
  SIGNATURE_FIELD_COUNT
} signature_field;

static uint32_t signature_field_of(const char *key, uint32_t key_size) {
  switch (key_size) {
  case KEY_LEN(GRDM_JSON_KEY_SIGNATURE):
    return KEY_IS(key, GRDM_JSON_KEY_SIGNATURE) ? SIGNATURE_FIELD_SIGNATURE : SIGNATURE_FIELD_NONE;
  case KEY_LEN(GRDM_JSON_KEY_PUBLIC_KEY):
    return KEY_IS(key, GRDM_JSON_KEY_PUBLIC_KEY) ? SIGNATURE_FIELD_PUBLIC_KEY
                                                 : SIGNATURE_FIELD_NONE;
  default:
    return SIGNATURE_FIELD_NONE;
  }
}

// ********** enumerations, read back through the names they were written by ****************

/*
 * Every enumeration in this project already has a to_string() that answers its enumerator's own
 * spelling, and these six walk that answer backwards: a candidate value is named and compared,
 * and the first name that matches is the value. No second table is written, so there is no
 * second table to fall out of step with the first -- a value added to an enum is readable here
 * the moment its name is added there.
 */

/** @brief Value of the transaction type @p name spells. */
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
 * All of these take a value the walk already found, so none of them searches for anything. A
 * NULL is a member the document did not carry, and every one of them refuses it: what a
 * transaction type owns is required, and a silent zero in a public key or an amount is the
 * expensive kind of forgiveness. Only the caller knows which members are optional, so only the
 * caller tests for NULL before it gets here.
 */

/**
 * @brief Read a hex string into a field whose length is fixed.
 *
 * @param[in]  value Member to read, or NULL where the object did not carry it.
 * @param[out] out   @p size bytes; untouched unless the call succeeds.
 * @param[in]  size  Bytes the field holds; the string has to be exactly twice as long.
 * @retval ARNM_SUCCESS                 The bytes are in @p out.
 * @retval ARNM_ERROR_DECODE_FAILED     @p value is absent, or is not @p size bytes of hex.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE @p value is there and is no string.
 */
static arnm_result read_hex_fixed(const arnm_json_value *value, uint8_t *out, uint32_t size) {
  if (!value) { return ARNM_ERROR_DECODE_FAILED; }
  const char *hex = NULL;
  uint32_t length = 0;
  const arnm_result result = arnm_json_read_string(value, &hex, &length);
  if (ARNM_SUCCESS != result) { return result; }
  if (length != size * 2u) { return ARNM_ERROR_DECODE_FAILED; }
  return arnm_binary_from_hex(out, hex);
}

/**
 * @brief Read a uuid in the canonical 8-4-4-4-12 form into 16 bytes.
 *
 * @param[in]  value Member to read, or NULL where the object did not carry it.
 * @param[out] out   @ref ARNM_UUID_BINARY_SIZE bytes; untouched unless the call succeeds.
 * @retval ARNM_SUCCESS                 The bytes are in @p out.
 * @retval ARNM_ERROR_DECODE_FAILED     @p value is absent, or is no uuid.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE @p value is there and is no string.
 */
static arnm_result read_uuid(const arnm_json_value *value, uint8_t *out) {
  if (!value) { return ARNM_ERROR_DECODE_FAILED; }
  const char *text = NULL;
  uint32_t length = 0;
  const arnm_result result = arnm_json_read_string(value, &text, &length);
  if (ARNM_SUCCESS != result) { return result; }
  if (ARNM_UUID_STRING_LENGTH != length) { return ARNM_ERROR_DECODE_FAILED; }
  return arnm_uuid_from_string(out, text);
}

/**
 * @brief Bytes a base64 string of @p length characters decodes to, padding taken off.
 *
 * The one place that answers this, because three callers have to agree on it: the pass that
 * sizes the arena, the pass that sizes a memo inside it, and the read that fills the block.
 * A sizing that comes out under what the read then writes is an arena the read runs past.
 *
 * @param[in]  text   The string; not NULL. Only its last two characters are looked at.
 * @param[in]  length Characters in @p text, terminator not counted.
 * @param[out] size   Receives the decoded length. Untouched unless the call succeeds.
 * @retval ARNM_SUCCESS             @p size holds what a decode would write.
 * @retval ARNM_ERROR_DECODE_FAILED @p length is not a multiple of four.
 */
static arnm_result base64_binary_size(const char *text, uint32_t length, uint32_t *size) {
  // four characters make three bytes, so anything else was never base64
  if (length % 4u) { return ARNM_ERROR_DECODE_FAILED; }
  if (!length) {
    *size = 0;
    return ARNM_SUCCESS;
  }
  uint32_t padding = 0;
  if ('=' == text[length - 1u]) { padding = ('=' == text[length - 2u]) ? 2u : 1u; }
  *size = ARNM_BASE64_BINARY_SIZE(length) - padding;
  return ARNM_SUCCESS;
}

/**
 * @brief Read a base64 string of any length into a block drawn from @p memory.
 *
 * @p out is cleared first, so an empty string leaves the empty block the writer produced it
 * from. Only bytes that are really there cost an allocation.
 *
 * @param[out]    out    Block to fill; not NULL. Written in full, read not at all.
 * @param[in]     value  Member to read, or NULL where the object did not carry it.
 * @param[in,out] memory Where the bytes come from -- the transaction's own arena.
 * @retval ARNM_SUCCESS                 The bytes are in @p out, or there were none to take.
 * @retval ARNM_ERROR_DECODE_FAILED     @p value is absent, or is not base64.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE @p value is there and is no string.
 * @retval Anything arnm_memory_block_alloc() can return.
 */
static arnm_result read_base64_block(
    arnm_memory_block *out, const arnm_json_value *value, arnm *memory
) {
  out->data = NULL;
  out->size = 0;
  if (!value) { return ARNM_ERROR_DECODE_FAILED; }

  const char *text = NULL;
  uint32_t length = 0;
  arnm_result result = arnm_json_read_string(value, &text, &length);
  if (ARNM_SUCCESS != result) { return result; }

  uint32_t size = 0;
  result = base64_binary_size(text, length, &size);
  if (ARNM_SUCCESS != result) { return result; }
  if (!size) { return ARNM_SUCCESS; }

  result = arnm_memory_block_alloc(out, size, memory);
  if (ARNM_SUCCESS != result) { return result; }

  uint32_t written = 0;
  result = arnm_binary_from_base64(out->data, &written, text);
  if (ARNM_SUCCESS != result) { return result; }
  // the block was reserved from the same answer, so a disagreement is this file contradicting
  // itself rather than a document being wrong
  return (written == size) ? ARNM_SUCCESS : ARNM_ERROR_DECODE_FAILED;
}

/**
 * @brief Read a string member, for the enumerations that arrive as their own spelling.
 *
 * @param[out] out   Receives the bytes, borrowed from the document and NUL terminated.
 * @param[in]  value Member to read, or NULL where the object did not carry it.
 * @retval ARNM_SUCCESS                 @p out points into the document.
 * @retval ARNM_ERROR_DECODE_FAILED     @p value is absent.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE @p value is there and is no string.
 */
static arnm_result read_enum_name(const char **out, const arnm_json_value *value) {
  if (!value) { return ARNM_ERROR_DECODE_FAILED; }
  return arnm_json_read_string(value, out, NULL);
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
 * @param[out] out    Timestamp to fill; untouched unless the call succeeds.
 * @param[in]  object The object, or NULL where the enclosing walk did not find it.
 */
static arnm_result read_timestamp(grdd_timestamp *out, const arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  arnm_json_value *member[TIMESTAMP_FIELD_COUNT];
  arnm_result result = collect_members(object, member, TIMESTAMP_FIELD_COUNT, timestamp_field_of);
  if (ARNM_SUCCESS != result) { return result; }

  int64_t seconds = 0;
  result = read_int64(&seconds, member[TIMESTAMP_FIELD_SECONDS]);
  if (ARNM_SUCCESS != result) { return result; }
  if (!member[TIMESTAMP_FIELD_NANOS]) { return ARNM_ERROR_DECODE_FAILED; }
  int32_t nanos = 0;
  result = arnm_json_read_int32(member[TIMESTAMP_FIELD_NANOS], &nanos);
  if (ARNM_SUCCESS != result) { return result; }

  out->seconds = seconds;
  out->nanos = nanos;
  return ARNM_SUCCESS;
}

/** @brief Read the three numbers of a hiero account id. */
static arnm_result read_account_id(grdw_hiero_account_id *out, const arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  arnm_json_value *member[ACCOUNT_ID_FIELD_COUNT];
  arnm_result result = collect_members(object, member, ACCOUNT_ID_FIELD_COUNT, account_id_field_of);
  if (ARNM_SUCCESS != result) { return result; }

  result = read_int64(&out->shardNum, member[ACCOUNT_ID_FIELD_SHARD_NUM]);
  if (ARNM_SUCCESS != result) { return result; }
  result = read_int64(&out->realmNum, member[ACCOUNT_ID_FIELD_REALM_NUM]);
  if (ARNM_SUCCESS != result) { return result; }
  return read_int64(&out->accountNum, member[ACCOUNT_ID_FIELD_ACCOUNT_NUM]);
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
  arnm_json_value *member[ANCHOR_FIELD_COUNT];
  arnm_result result = collect_members(object, member, ANCHOR_FIELD_COUNT, anchor_field_of);
  if (ARNM_SUCCESS != result) { return result; }

  const char *type_name = NULL;
  result = read_enum_name(&type_name, member[ANCHOR_FIELD_TYPE]);
  if (ARNM_SUCCESS != result) { return result; }

  out->type = GRDT_LEDGER_ANCHOR_UNSPECIFIED;
  out->id = 0;
  result = ledger_anchor_from_string(&out->type, type_name);
  if (ARNM_SUCCESS != result) { return result; }

  switch (out->type) {
  case GRDT_LEDGER_ANCHOR_UNSPECIFIED:
    return ARNM_SUCCESS;
  case GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID: {
    if (!member[ANCHOR_FIELD_HIERO_TRANSACTION_ID]) { return ARNM_ERROR_DECODE_FAILED; }
    arnm_json_value *hiero_member[HIERO_FIELD_COUNT];
    result = collect_members(
        member[ANCHOR_FIELD_HIERO_TRANSACTION_ID], hiero_member, HIERO_FIELD_COUNT, hiero_field_of
    );
    if (ARNM_SUCCESS != result) { return result; }
    result = read_timestamp(
        &out->hiero_transaction_id.transactionValidStart,
        hiero_member[HIERO_FIELD_TRANSACTION_VALID_START]
    );
    if (ARNM_SUCCESS != result) { return result; }
    return read_account_id(
        &out->hiero_transaction_id.accountID, hiero_member[HIERO_FIELD_ACCOUNT_ID]
    );
  }
  default:
    return read_uint64(&out->id, member[ANCHOR_FIELD_ID]);
  }
}

// ********** the transaction's detail, branch by branch ************************************

/** @brief Read the transfer branch, which serves a creation and both deferred transfers too. */
static arnm_result read_transfer(grdr_complete_transaction *tx, const arnm_json_value *object) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  arnm_json_value *member[TRANSFER_FIELD_COUNT];
  arnm_result result = collect_members(object, member, TRANSFER_FIELD_COUNT, transfer_field_of);
  if (ARNM_SUCCESS != result) { return result; }

  result = read_hex_fixed(
      member[TRANSFER_FIELD_SENDER_PUBKEY], tx->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  result = read_hex_fixed(
      member[TRANSFER_FIELD_RECIPIENT_PUBKEY], tx->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  result = read_int64(&tx->transfer.amount, member[TRANSFER_FIELD_AMOUNT]);
  if (ARNM_SUCCESS != result) { return result; }
  return read_uuid(member[TRANSFER_FIELD_COIN_COMMUNITY_UUID], tx->transfer.coin_community_uuid);
}

/** @brief Read the register-address branch, address type and derivation index included. */
static arnm_result read_register_address(
    grdr_complete_transaction *tx, const arnm_json_value *object
) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  arnm_json_value *member[REGISTER_ADDRESS_FIELD_COUNT];
  arnm_result result =
      collect_members(object, member, REGISTER_ADDRESS_FIELD_COUNT, register_address_field_of);
  if (ARNM_SUCCESS != result) { return result; }

  result = read_hex_fixed(
      member[REGISTER_ADDRESS_FIELD_USER_PUBLIC_KEY], tx->register_address.user_public_key,
      SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  result = read_hex_fixed(
      member[REGISTER_ADDRESS_FIELD_NAME_HASH], tx->register_address.name_hash, GENERIC_HASH_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  result = read_hex_fixed(
      member[REGISTER_ADDRESS_FIELD_ACCOUNT_PUBLIC_KEY], tx->register_address.account_public_key,
      SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }

  // the second union, not the one above: same transaction, other half of the struct
  const char *address_name = NULL;
  result = read_enum_name(&address_name, member[REGISTER_ADDRESS_FIELD_ADDRESS_TYPE]);
  if (ARNM_SUCCESS != result) { return result; }
  result = address_from_string(&tx->address_type, address_name);
  if (ARNM_SUCCESS != result) { return result; }

  if (!member[REGISTER_ADDRESS_FIELD_DERIVATION_INDEX]) { return ARNM_ERROR_DECODE_FAILED; }
  return arnm_json_read_uint32(
      member[REGISTER_ADDRESS_FIELD_DERIVATION_INDEX], &tx->derivation_index
  );
}

/** @brief Read the community-root branch: the community key and its two account keys. */
static arnm_result read_community_root(
    grdr_complete_transaction *tx, const arnm_json_value *object
) {
  if (!object) { return ARNM_ERROR_DECODE_FAILED; }
  arnm_json_value *member[COMMUNITY_ROOT_FIELD_COUNT];
  arnm_result result =
      collect_members(object, member, COMMUNITY_ROOT_FIELD_COUNT, community_root_field_of);
  if (ARNM_SUCCESS != result) { return result; }

  result = read_hex_fixed(
      member[COMMUNITY_ROOT_FIELD_PUBLIC_KEY], tx->community_root.public_key, SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  result = read_hex_fixed(
      member[COMMUNITY_ROOT_FIELD_GMW_PUBLIC_KEY], tx->community_root.gmw_public_key,
      SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  return read_hex_fixed(
      member[COMMUNITY_ROOT_FIELD_AUF_PUBLIC_KEY], tx->community_root.auf_public_key,
      SIGN_PUBLIC_KEY_SIZE
  );
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
static arnm_result memo_payload_size(uint64_t *total, const arnm_json_value *array) {
  arnm_json_array_iter iter;
  if (ARNM_SUCCESS != arnm_json_array_iter_init(array, &iter)) {
    return ARNM_ERROR_INVALID_ENUM_TYPE;
  }
  arnm_json_value *element = NULL;
  while (arnm_json_array_iter_next(&iter, &element)) {
    arnm_json_value *member[MEMO_FIELD_COUNT];
    const arnm_result result = collect_members(element, member, MEMO_FIELD_COUNT, memo_field_of);
    if (ARNM_SUCCESS != result) { return result; }
    if (!member[MEMO_FIELD_MEMO]) { return ARNM_ERROR_DECODE_FAILED; }

    const char *text = NULL;
    uint32_t length = 0;
    const arnm_result read = arnm_json_read_string(member[MEMO_FIELD_MEMO], &text, &length);
    if (ARNM_SUCCESS != read) { return read; }
    uint32_t size = 0;
    const arnm_result sized = base64_binary_size(text, length, &size);
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

  arnm_result result = array_length(&count, present(root[ROOT_FIELD_ACCOUNT_BALANCES]));
  if (ARNM_SUCCESS != result) { return result; }
  total += ARNM_ALIGN8((uint64_t)count * sizeof(grdw_account_balance));

  result = array_length(&count, present(root[ROOT_FIELD_ENCRYPTED_MEMOS]));
  if (ARNM_SUCCESS != result) { return result; }
  total += ARNM_ALIGN8((uint64_t)count * sizeof(grdw_encrypted_memo));
  if (count) {
    result = memo_payload_size(&total, present(root[ROOT_FIELD_ENCRYPTED_MEMOS]));
    if (ARNM_SUCCESS != result) { return result; }
  }

  result = array_length(&count, present(root[ROOT_FIELD_SIGNATURE_PAIRS]));
  if (ARNM_SUCCESS != result) { return result; }
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
    result = base64_binary_size(text, length, &size);
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
    arnm_json_value *member[BALANCE_FIELD_COUNT];
    result = collect_members(element, member, BALANCE_FIELD_COUNT, balance_field_of);
    if (ARNM_SUCCESS != result) { return result; }

    result = read_hex_fixed(member[BALANCE_FIELD_PUBKEY], balance->pubkey, SIGN_PUBLIC_KEY_SIZE);
    if (ARNM_SUCCESS != result) { return result; }
    result = read_int64(&balance->balance, member[BALANCE_FIELD_BALANCE]);
    if (ARNM_SUCCESS != result) { return result; }
    result = read_uuid(member[BALANCE_FIELD_COMMUNITY_UUID], balance->community_uuid);
    if (ARNM_SUCCESS != result) { return result; }
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
    arnm_json_value *member[MEMO_FIELD_COUNT];
    result = collect_members(element, member, MEMO_FIELD_COUNT, memo_field_of);
    if (ARNM_SUCCESS != result) { return result; }

    const char *type_name = NULL;
    result = read_enum_name(&type_name, member[MEMO_FIELD_TYPE]);
    if (ARNM_SUCCESS != result) { return result; }
    result = memo_key_from_string(&memo->type, type_name);
    if (ARNM_SUCCESS != result) { return result; }
    result = read_base64_block(&memo->memo, member[MEMO_FIELD_MEMO], &tx->memory_area);
    if (ARNM_SUCCESS != result) { return result; }
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
    arnm_json_value *member[SIGNATURE_FIELD_COUNT];
    result = collect_members(element, member, SIGNATURE_FIELD_COUNT, signature_field_of);
    if (ARNM_SUCCESS != result) { return result; }

    result =
        read_hex_fixed(member[SIGNATURE_FIELD_PUBLIC_KEY], pair->public_key, SIGN_PUBLIC_KEY_SIZE);
    if (ARNM_SUCCESS != result) { return result; }
    result =
        read_hex_fixed(member[SIGNATURE_FIELD_SIGNATURE], pair->signature, SIGN_SIGNATURE_SIZE);
    if (ARNM_SUCCESS != result) { return result; }
    ++filled;
  }
  tx->signature_pairs_count = filled;
  return ARNM_SUCCESS;
}

/** @brief Read the two members only a transaction that is not local ever carries. */
static arnm_result read_cross_group(grdr_complete_transaction *tx, arnm_json_value *const *root) {
  const char *cross_group_name = NULL;
  arnm_result result = read_enum_name(&cross_group_name, root[ROOT_FIELD_CROSS_GROUP_TYPE]);
  if (ARNM_SUCCESS != result) { return result; }
  result = cross_group_from_string(&tx->cross_group_type, cross_group_name);
  if (ARNM_SUCCESS != result) { return result; }

  if (present(root[ROOT_FIELD_TX_PAIRING_COMMUNITY_UUID])) {
    result = arnm_alloc(&tx->tx_pairing_community_uuid, ARNM_UUID_BINARY_SIZE, &tx->memory_area);
    if (ARNM_SUCCESS != result) { return result; }
    result = read_uuid(
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
  result = read_uuid(root[ROOT_FIELD_TX_COMMUNITY_UUID], tx->tx_community_uuid);
  if (ARNM_SUCCESS != result) { return result; }
  result = read_ledger_anchor(&tx->ledger_anchor, root[ROOT_FIELD_LEDGER_ANCHOR]);
  if (ARNM_SUCCESS != result) { return result; }

  const char *transaction_name = NULL;
  result = read_enum_name(&transaction_name, root[ROOT_FIELD_TRANSACTION_TYPE]);
  if (ARNM_SUCCESS != result) { return result; }
  result = transaction_from_string(&tx->transaction_type, transaction_name);
  if (ARNM_SUCCESS != result) { return result; }

  const char *derivation_name = NULL;
  result = read_enum_name(&derivation_name, root[ROOT_FIELD_BALANCE_DERIVATION_TYPE]);
  if (ARNM_SUCCESS != result) { return result; }
  result = balance_derivation_from_string(&tx->balance_derivation_type, derivation_name);
  if (ARNM_SUCCESS != result) { return result; }

  result = read_hex_fixed(root[ROOT_FIELD_TX_RUNNING_HASH], tx->tx_running_hash, GENERIC_HASH_SIZE);
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

  return read_base64_block(&tx->body_bytes, root[ROOT_FIELD_BODY_BYTES], &tx->memory_area);
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
    arnm_json_value *root[ROOT_FIELD_COUNT];
    result = collect_members(arnm_json_reader_root(&reader), root, ROOT_FIELD_COUNT, root_field_of);

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
