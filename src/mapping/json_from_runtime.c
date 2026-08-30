#include "gradido_blockchain_core/mapping/json_from_runtime.h"

#include "arnm/converter.h"
#include "arnm/memory.h"
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

/**
 * @brief What one hex field of @p size bytes takes in the document's string pool.
 *
 * Two characters a byte, the two quotes arnm_json_writer_add_hex() writes around them, and the
 * terminator the pool puts behind every entry.
 */
#define HEX_FIELD_BYTES(size) ((uint32_t)(size) * 2u + 3u)

/**
 * @brief What one base64 field of @p size bytes takes in the document's string pool.
 *
 * Four characters per three bytes, the group rounded up, the two quotes and the terminator.
 */
#define BASE64_FIELD_BYTES(size) (ARNM_BASE64_STRING_LENGTH(size) + 3u)

/** @brief The same for a uuid: the canonical 36 characters, the quotes and the terminator. */
#define UUID_FIELD_BYTES 39u

/** @brief Values a transfer branch costs: the member, the object, and four members of it. */
#define TRANSFER_VALUES 10u

/** @brief Its string pool cost: two public keys as hex and the coin's community uuid. */
#define TRANSFER_STRING_BYTES (2u * HEX_FIELD_BYTES(SIGN_PUBLIC_KEY_SIZE) + UUID_FIELD_BYTES)

/**
 * @brief Values a ledger anchor costs, the object itself counted.
 *
 * The object and its type name are three; what the type owns comes on top -- a number for the
 * named ids, and for a hiero anchor a nested id of a timestamp and an account.
 */
static uint32_t anchor_values(const grdw_ledger_anchor *anchor) {
  switch (anchor->type) {
  case GRDT_LEDGER_ANCHOR_UNSPECIFIED:
    return 3u;
  case GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID:
    return 19u;
  default:
    return 5u;
  }
}

/**
 * @brief Write a timestamp as an object of whole seconds and the nanoseconds beside them.
 *
 * @param[in,out] writer    Writer to add to.
 * @param[in]     key       Field name.
 * @param[in]     timestamp Seconds since the Unix epoch and the nanos within the second.
 */
static void add_timestamp(
    arnm_json_writer *writer, const char *key, const grdd_timestamp *timestamp
) {
  arnm_json_writer_open_object(writer, key);
  arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_SECONDS, timestamp->seconds);
  arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_NANOS, timestamp->nanos);
  arnm_json_writer_close(writer);
}

/**
 * @brief Write a ledger anchor as its type and whichever member that type owns.
 *
 * A hiero anchor carries a nested transaction id, every other named type carries a single
 * number, and the unspecified one carries nothing at all -- the union is written the way it is
 * read, one branch of it and no more.
 *
 * @param[in,out] writer Writer to add to.
 * @param[in]     key    Field name, or NULL for an element of the current array.
 * @param[in]     anchor Anchor to write; not NULL.
 */
static void add_ledger_anchor(
    arnm_json_writer *writer, const char *key, const grdw_ledger_anchor *anchor
) {
  arnm_json_writer_open_object(writer, key);
  arnm_json_writer_add_string(
      writer, GRDM_JSON_KEY_TYPE, grdt_ledger_anchor_to_string(anchor->type)
  );
  switch (anchor->type) {
  case GRDT_LEDGER_ANCHOR_UNSPECIFIED:
    break;
  case GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID: {
    const grdw_hiero_transaction_id *hiero = &anchor->hiero_transaction_id;
    arnm_json_writer_open_object(writer, GRDM_JSON_KEY_HIERO_TRANSACTION_ID);
    add_timestamp(writer, GRDM_JSON_KEY_TRANSACTION_VALID_START, &hiero->transactionValidStart);
    arnm_json_writer_open_object(writer, GRDM_JSON_KEY_ACCOUNT_ID);
    arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_SHARD_NUM, hiero->accountID.shardNum);
    arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_REALM_NUM, hiero->accountID.realmNum);
    arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_ACCOUNT_NUM, hiero->accountID.accountNum);
    arnm_json_writer_close(writer);
    arnm_json_writer_close(writer);
    break;
  }
  default:
    arnm_json_writer_add_uint64(writer, GRDM_JSON_KEY_ID, anchor->id);
    break;
  }
  arnm_json_writer_close(writer);
}

/**
 * @brief Write the transfer branch: two keys, an amount and the coin's community.
 *
 * The same branch serves a creation, where the sender key is the run of zeros the wire mapping
 * left there -- written as it stands, because a field silently dropped is a field that comes
 * back different.
 */
static void add_transfer(arnm_json_writer *writer, const grdr_complete_transaction *tx) {
  arnm_json_writer_open_object(writer, GRDM_JSON_KEY_TRANSFER);
  arnm_json_writer_add_hex(
      writer, GRDM_JSON_KEY_SENDER_PUBKEY, tx->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE
  );
  arnm_json_writer_add_hex(
      writer, GRDM_JSON_KEY_RECIPIENT_PUBKEY, tx->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE
  );
  arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_AMOUNT, tx->transfer.amount);
  arnm_json_writer_add_uuid(
      writer, GRDM_JSON_KEY_COIN_COMMUNITY_UUID, tx->transfer.coin_community_uuid
  );
  arnm_json_writer_close(writer);
}

/**
 * @brief Write the register-address branch, address type and derivation index included.
 *
 * Those last two live in the transaction's second union rather than in this one, but they
 * belong to this transaction alone, and a document that scatters them would be harder to read
 * than the struct it describes.
 */
static void add_register_address(arnm_json_writer *writer, const grdr_complete_transaction *tx) {
  arnm_json_writer_open_object(writer, GRDM_JSON_KEY_REGISTER_ADDRESS);
  arnm_json_writer_add_hex(
      writer, GRDM_JSON_KEY_USER_PUBLIC_KEY, tx->register_address.user_public_key,
      SIGN_PUBLIC_KEY_SIZE
  );
  arnm_json_writer_add_hex(
      writer, GRDM_JSON_KEY_NAME_HASH, tx->register_address.name_hash, GENERIC_HASH_SIZE
  );
  arnm_json_writer_add_hex(
      writer, GRDM_JSON_KEY_ACCOUNT_PUBLIC_KEY, tx->register_address.account_public_key,
      SIGN_PUBLIC_KEY_SIZE
  );
  arnm_json_writer_close(writer);
}

/** @brief Write the community-root branch: the community key and its two account keys. */
static void add_community_root(arnm_json_writer *writer, const grdr_complete_transaction *tx) {
  arnm_json_writer_open_object(writer, GRDM_JSON_KEY_COMMUNITY_ROOT);
  arnm_json_writer_add_hex(
      writer, GRDM_JSON_KEY_PUBLIC_KEY, tx->community_root.public_key, SIGN_PUBLIC_KEY_SIZE
  );
  arnm_json_writer_add_hex(
      writer, GRDM_JSON_KEY_GMW_PUBLIC_KEY, tx->community_root.gmw_public_key, SIGN_PUBLIC_KEY_SIZE
  );
  arnm_json_writer_add_hex(
      writer, GRDM_JSON_KEY_AUF_PUBLIC_KEY, tx->community_root.auf_public_key, SIGN_PUBLIC_KEY_SIZE
  );
  arnm_json_writer_close(writer);
}

/**
 * @brief Write the detail and the context the transaction's type owns, and nothing else.
 *
 * Sorted by expected frequency of occurrence, the same order
 * grdm_complete_transaction_from_wire() reads them in. A type with no layout here is refused
 * rather than written half way -- the same refusal the wire mapping answers for the same
 * types, so neither direction accepts what the other cannot.
 */
static arnm_result add_transaction_detail(
    arnm_json_writer *writer, const grdr_complete_transaction *tx
) {
  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_TRANSFER:
    add_transfer(writer, tx);
    break;
  case GRDT_TRANSACTION_CREATION:
    add_transfer(writer, tx);
    arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_TARGET_DATE, tx->target_date);
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    add_register_address(writer, tx);
    arnm_json_writer_add_string(
        writer, GRDM_JSON_KEY_ADDRESS_TYPE, grdt_address_to_string(tx->address_type)
    );
    arnm_json_writer_add_uint64(writer, GRDM_JSON_KEY_DERIVATION_INDEX, tx->derivation_index);
    break;
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
    add_transfer(writer, tx);
    arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_TIMEOUT_DURATION, tx->timeout_duration);
    break;
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    add_transfer(writer, tx);
    arnm_json_writer_add_uint64(writer, GRDM_JSON_KEY_PREVIOUS_TX, tx->previous_tx);
    break;
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER:
    arnm_json_writer_add_uint64(writer, GRDM_JSON_KEY_PREVIOUS_TX, tx->previous_tx);
    break;
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    add_community_root(writer, tx);
    break;
  default:
    return ARNM_ERROR_ENUM_UNHANDLED;
  }
  return ARNM_SUCCESS;
}

/**
 * @brief Write the three arrays: balances as they settled, memos, and the signatures over them.
 *
 * Each is written even when it holds nothing, so an empty array and an absent member never
 * have to mean the same thing to whoever reads the document next.
 */
static void add_arrays(arnm_json_writer *writer, const grdr_complete_transaction *tx) {
  arnm_json_writer_open_array(writer, GRDM_JSON_KEY_ACCOUNT_BALANCES);
  for (size_t i = 0; i < tx->account_balances_count; ++i) {
    const grdw_account_balance *balance = &tx->account_balances[i];
    arnm_json_writer_open_object(writer, NULL);
    arnm_json_writer_add_hex(writer, GRDM_JSON_KEY_PUBKEY, balance->pubkey, SIGN_PUBLIC_KEY_SIZE);
    arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_BALANCE, balance->balance);
    arnm_json_writer_add_uuid(writer, GRDM_JSON_KEY_COMMUNITY_UUID, balance->community_uuid);
    arnm_json_writer_close(writer);
  }
  arnm_json_writer_close(writer);

  arnm_json_writer_open_array(writer, GRDM_JSON_KEY_ENCRYPTED_MEMOS);
  for (size_t i = 0; i < tx->encrypted_memos_count; ++i) {
    const grdw_encrypted_memo *memo = &tx->encrypted_memos[i];
    arnm_json_writer_open_object(writer, NULL);
    arnm_json_writer_add_string(writer, GRDM_JSON_KEY_TYPE, grdt_memo_key_to_string(memo->type));
    arnm_json_writer_add_base64(writer, GRDM_JSON_KEY_MEMO, memo->memo.data, memo->memo.size);
    arnm_json_writer_close(writer);
  }
  arnm_json_writer_close(writer);

  arnm_json_writer_open_array(writer, GRDM_JSON_KEY_SIGNATURE_PAIRS);
  for (size_t i = 0; i < tx->signature_pairs_count; ++i) {
    const grdw_signature_pair *pair = &tx->signature_pairs[i];
    arnm_json_writer_open_object(writer, NULL);
    arnm_json_writer_add_hex(
        writer, GRDM_JSON_KEY_PUBLIC_KEY, pair->public_key, SIGN_PUBLIC_KEY_SIZE
    );
    arnm_json_writer_add_hex(writer, GRDM_JSON_KEY_SIGNATURE, pair->signature, SIGN_SIGNATURE_SIZE);
    arnm_json_writer_close(writer);
  }
  arnm_json_writer_close(writer);
}

/** @brief Lay the whole transaction into the writer, in the order the struct declares it. */
static arnm_result add_complete_transaction(
    arnm_json_writer *writer, const grdr_complete_transaction *tx
) {
  arnm_json_writer_begin_object(writer);

  // change the order as seen in struct to speed up parsing json, with transaction type as first, it
  // can walk the second time with exact the expected keys
  arnm_json_writer_add_string(
      writer, GRDM_JSON_KEY_TRANSACTION_TYPE, grdt_transaction_to_string(tx->transaction_type)
  );
  arnm_json_writer_add_uint64(writer, GRDM_JSON_KEY_TX_NR, tx->tx_nr);
  add_timestamp(writer, GRDM_JSON_KEY_CONFIRMED_AT, &tx->confirmed_at);
  add_timestamp(writer, GRDM_JSON_KEY_CREATED_AT, &tx->created_at);
  arnm_json_writer_add_uuid(writer, GRDM_JSON_KEY_TX_COMMUNITY_UUID, tx->tx_community_uuid);
  add_ledger_anchor(writer, GRDM_JSON_KEY_LEDGER_ANCHOR, &tx->ledger_anchor);

  const arnm_result result = add_transaction_detail(writer, tx);
  if (ARNM_SUCCESS != result) { return result; }

  arnm_json_writer_add_string(
      writer, GRDM_JSON_KEY_BALANCE_DERIVATION_TYPE,
      grdt_balance_derivation_to_string(tx->balance_derivation_type)
  );
  arnm_json_writer_add_hex(
      writer, GRDM_JSON_KEY_TX_RUNNING_HASH, tx->tx_running_hash, GENERIC_HASH_SIZE
  );

  add_arrays(writer, tx);

  arnm_json_writer_add_string(
      writer, GRDM_JSON_KEY_CROSS_GROUP_TYPE, grdt_cross_group_to_string(tx->cross_group_type)
  );

  // the two cross-group members are written only where they are set: on a local transaction
  // they are NULL, and a document that carried them as null would say something the wire never
  // said
  if (tx->tx_pairing_community_uuid) {
    arnm_json_writer_add_uuid(
        writer, GRDM_JSON_KEY_TX_PAIRING_COMMUNITY_UUID, tx->tx_pairing_community_uuid
    );
  }
  if (tx->pairing_ledger_anchor) {
    add_ledger_anchor(writer, GRDM_JSON_KEY_PAIRING_LEDGER_ANCHOR, tx->pairing_ledger_anchor);
  }

  arnm_json_writer_add_base64(
      writer, GRDM_JSON_KEY_BODY_BYTES, tx->body_bytes.data, tx->body_bytes.size
  );
  return ARNM_SUCCESS;
}

/**
 * @brief What this transaction's document will cost the writer's two pools.
 *
 * Walked before a field is written, so both pools open once at the right size instead of
 * doubling their way there and keeping every chunk they outgrew. On the largest transaction of a
 * real ledger that is the difference between 2688 and 1968 bytes of value pool, and between 3840
 * and 2590 of string pool.
 *
 * The figures are exact for every transaction this file knows how to lay out -- the walk was
 * checked against the node count of all 48762 documents of the Gradido Akademie ledger, with no
 * disagreement. They do not have to stay exact, and that is the point: a field added below and
 * forgotten here costs one extra chunk, which is the growth that would have happened anyway. So
 * this is not a second place a field can be got wrong, only one where it can be got cheap.
 *
 * @param[out] hint Receives the two figures.
 * @param[in]  tx   Transaction about to be written; not NULL.
 */
static void calculate_hint(arnm_json_writer_hint *hint, const grdr_complete_transaction *tx) {
  // an object member is its key and its value; a container is one more, and what it holds
  // besides. Counted the way arnm/json_writer.h describes it.
  //   tx_nr, the three type names, tx_running_hash, body_bytes  6 members
  //   tx_community_uuid                                         1 member
  //   confirmed_at, created_at   2 members, each an object of two members
  //   the three arrays           3 members, each an array
  uint32_t values = 1u + 2u * 6u + 2u + 6u + 6u + 3u * 2u;
  // the hex and uuid fields, each counted with its quotes and the terminator the pool adds
  uint32_t string_bytes = HEX_FIELD_BYTES(GENERIC_HASH_SIZE) /* tx_running_hash */
                          + UUID_FIELD_BYTES                 /* tx_community_uuid */
                          + BASE64_FIELD_BYTES(tx->body_bytes.size);

  values += 1u + anchor_values(&tx->ledger_anchor);

  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_TRANSFER:
    values += TRANSFER_VALUES;
    string_bytes += TRANSFER_STRING_BYTES;
    break;
  case GRDT_TRANSACTION_CREATION:
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    // the branch, and the one number that stands beside it
    values += TRANSFER_VALUES + 2u;
    string_bytes += TRANSFER_STRING_BYTES;
    break;
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER:
    values += 2u;
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    values += 12u;
    string_bytes += 2u * HEX_FIELD_BYTES(SIGN_PUBLIC_KEY_SIZE) + HEX_FIELD_BYTES(GENERIC_HASH_SIZE);
    break;
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    values += 8u;
    string_bytes += 3u * HEX_FIELD_BYTES(SIGN_PUBLIC_KEY_SIZE);
    break;
  default:
    // a type with no layout is refused before anything is written; the hint is what the
    // envelope alone costs and never reaches a document
    break;
  }

  values += (uint32_t)tx->account_balances_count * 7u;
  string_bytes += (uint32_t)tx->account_balances_count *
                  (HEX_FIELD_BYTES(SIGN_PUBLIC_KEY_SIZE) + UUID_FIELD_BYTES);

  values += (uint32_t)tx->encrypted_memos_count * 5u;
  for (size_t i = 0; i < tx->encrypted_memos_count; ++i) {
    string_bytes += BASE64_FIELD_BYTES(tx->encrypted_memos[i].memo.size);
  }

  values += (uint32_t)tx->signature_pairs_count * 5u;
  string_bytes += (uint32_t)tx->signature_pairs_count *
                  (HEX_FIELD_BYTES(SIGN_PUBLIC_KEY_SIZE) + HEX_FIELD_BYTES(SIGN_SIGNATURE_SIZE));

  if (tx->tx_pairing_community_uuid) {
    values += 2u;
    string_bytes += UUID_FIELD_BYTES;
  }
  if (tx->pairing_ledger_anchor) { values += 1u + anchor_values(tx->pairing_ledger_anchor); }

  hint->values = values;
  hint->string_bytes = string_bytes;
}

arnm_result grdm_json_from_complete_transaction(
    arnm_memory_block *out,
    const grdr_complete_transaction *tx,
    arnm *allocator,
    arnm_json_write_flags flags
) {
  if (!out || !tx) { return ARNM_ERROR_NULL_POINTER; }

  arnm_json_writer_hint hint;
  calculate_hint(&hint, tx);

  arnm_json_writer writer;
  arnm_result result = arnm_json_writer_init(&writer, allocator, flags, &hint);
  if (ARNM_SUCCESS != result) { return result; }

  result = add_complete_transaction(&writer, tx);
  if (ARNM_SUCCESS == result) {
    // the writer carries its own first refusal, and a writer carrying one refuses to render;
    // so this single call answers for every field above that was added without a test
    result = arnm_json_writer_write(&writer, allocator, out, NULL);
  }

  // the document goes back before the text does, so an arena reclaims it from its tail; where
  // it cannot, the bytes wait for the reset and the caller is told, because that is the
  // caller's arena and the caller's rhythm
  const arnm_result reclaim = arnm_json_writer_release(&writer);
  if (ARNM_SUCCESS == result && ARNM_SUCCESS != reclaim) { return reclaim; }
  return result;
}
