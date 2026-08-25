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

#include <assert.h>
#include <stdint.h>

/* C11 static assert fallback; in C++ the keyword is already there */
#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert _Static_assert
#endif

/**
 * @brief Bytes of the longest fixed-size binary field, which sizes the one stack buffer below.
 *
 * A signature at 64 bytes is the tallest of them; the public keys and the hashes are half
 * that. The static_asserts that follow hold the number against every field it has to cover, so
 * a field that grows takes this with it instead of quietly running past a buffer.
 */
#define FIXED_HEX_MAX_BYTES SIGN_SIGNATURE_SIZE

static_assert(SIGN_PUBLIC_KEY_SIZE <= FIXED_HEX_MAX_BYTES, "public key outgrew the hex buffer");
static_assert(SIGN_SIGNATURE_SIZE <= FIXED_HEX_MAX_BYTES, "signature outgrew the hex buffer");
static_assert(GENERIC_HASH_SIZE <= FIXED_HEX_MAX_BYTES, "generic hash outgrew the hex buffer");

/**
 * @brief Write @p size bytes as a hex string field, through a buffer on the stack.
 *
 * For the fields whose length the type system already fixed -- keys, hashes, signatures. The
 * text is copied into the document rather than borrowed, because the buffer it was formatted
 * in is gone one line later.
 *
 * @param[in,out] writer Writer to add to.
 * @param[in]     key    Field name.
 * @param[in]     data   Bytes to render; not NULL.
 * @param[in]     size   How many, at most @ref FIXED_HEX_MAX_BYTES and greater than 0.
 * @retval ARNM_SUCCESS                          The field is in the document.
 * @retval ARNM_ERROR_DESTINATION_BUFFER_TO_SMALL @p size is past what the buffer covers.
 * @retval Anything arnm_binary_to_hex() can return.
 */
static arnm_result add_hex_fixed(
    arnm_json_writer *writer, const char *key, const uint8_t *data, uint32_t size
) {
  char text[FIXED_HEX_MAX_BYTES * 2u + 1u];
  if (size > FIXED_HEX_MAX_BYTES) { return ARNM_ERROR_DESTINATION_BUFFER_TO_SMALL; }
  const arnm_memory_block block = {(uint8_t *)data, size};
  const arnm_result result = arnm_binary_to_hex(text, &block);
  if (ARNM_SUCCESS != result) { return result; }
  arnm_json_writer_add_string_copy(writer, key, text);
  return ARNM_SUCCESS;
}

/**
 * @brief Write 16 bytes as a uuid field in the canonical 8-4-4-4-12 form.
 *
 * @param[in,out] writer Writer to add to.
 * @param[in]     key    Field name.
 * @param[in]     uuid   The 16 bytes; not NULL. Any 16 bytes are a uuid here -- version and
 *                       variant are the caller's business, as they are in arnm.
 */
static void add_uuid(arnm_json_writer *writer, const char *key, const uint8_t *uuid) {
  char text[ARNM_UUID_STRING_LENGTH + 1u];
  arnm_uuid_to_string(text, uuid);
  arnm_json_writer_add_string_copy(writer, key, text);
}

/**
 * @brief Write a block of unknown length as a hex string field.
 *
 * The memos and `body_bytes` are the only two fields whose length the wire decides, so they
 * are the only two that need memory to be rendered. The hex is formatted into scratch drawn
 * from @p allocator, copied into the document, and the scratch handed straight back -- it is
 * younger than nothing and older than the copy, which is why an arena answers the reclaim with
 * a warning rather than with the bytes. That warning is scratch not coming back before the
 * arena resets; it says nothing about the field, which is written either way.
 *
 * An empty block is written as an empty string rather than as `null`, so the way back reads it
 * as the empty block it was and not as a member that was never there.
 *
 * The copy is what makes a long memo cost twice: the bytes are hexed into scratch and then
 * copied into the document. `bench_json` prints what that is worth -- on a transaction that is
 * nearly all payload the write comes out slower than the read because of it. Borrowing instead
 * would need every such block kept alive until the render and freed after, which is
 * bookkeeping this mapping does not carry today; the trade is written down here so the next
 * reader does not have to rediscover it.
 *
 * @param[in,out] writer    Writer to add to.
 * @param[in,out] allocator Where the scratch comes from, or NULL for the host.
 * @param[in]     key       Field name.
 * @param[in]     block     Bytes to render; not NULL, may be empty.
 * @retval ARNM_SUCCESS                    The field is in the document.
 * @retval ARNM_ERROR_RESOURCE_SIZE_EXCEED The hex of @p block cannot be counted in a uint32_t.
 * @retval Anything arnm_alloc() or arnm_binary_to_hex() can return.
 */
static arnm_result add_hex_block(
    arnm_json_writer *writer, arnm *allocator, const char *key, const arnm_memory_block *block
) {
  if (!block->data || !block->size) {
    arnm_json_writer_add_string(writer, key, "");
    return ARNM_SUCCESS;
  }
  if (block->size > (ARNM_MAX_ALLOC_SIZE - 1u) / 2u) { return ARNM_ERROR_RESOURCE_SIZE_EXCEED; }

  const uint32_t text_size = block->size * 2u + 1u;
  uint8_t *text = NULL;
  arnm_result result = arnm_alloc(&text, text_size, allocator);
  if (ARNM_SUCCESS != result) { return result; }

  result = arnm_binary_to_hex((char *)text, block);
  if (ARNM_SUCCESS == result) {
    arnm_json_writer_add_string_copy_length(writer, key, (const char *)text, text_size - 1u);
  }

  // the document's own copy sits above this block, so an arena cannot take it back from its
  // tail: expected here, and neither a failure nor something the field's fate depends on
  const arnm_result reclaim = arnm_free(text, text_size, allocator);
  if (ARNM_SUCCESS != reclaim && ARNM_WARNING_ARENA_MEMORY_NOT_RECLAIMED != reclaim) {
    return reclaim;
  }
  return result;
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
static arnm_result add_transfer(arnm_json_writer *writer, const grdr_complete_transaction *tx) {
  arnm_json_writer_open_object(writer, GRDM_JSON_KEY_TRANSFER);
  arnm_result result = add_hex_fixed(
      writer, GRDM_JSON_KEY_SENDER_PUBKEY, tx->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  result = add_hex_fixed(
      writer, GRDM_JSON_KEY_RECIPIENT_PUBKEY, tx->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_AMOUNT, tx->transfer.amount);
  add_uuid(writer, GRDM_JSON_KEY_COIN_COMMUNITY_UUID, tx->transfer.coin_community_uuid);
  arnm_json_writer_close(writer);
  return ARNM_SUCCESS;
}

/**
 * @brief Write the register-address branch, address type and derivation index included.
 *
 * Those last two live in the transaction's second union rather than in this one, but they
 * belong to this transaction alone, and a document that scatters them would be harder to read
 * than the struct it describes.
 */
static arnm_result add_register_address(
    arnm_json_writer *writer, const grdr_complete_transaction *tx
) {
  arnm_json_writer_open_object(writer, GRDM_JSON_KEY_REGISTER_ADDRESS);
  arnm_result result = add_hex_fixed(
      writer, GRDM_JSON_KEY_USER_PUBLIC_KEY, tx->register_address.user_public_key,
      SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  result = add_hex_fixed(
      writer, GRDM_JSON_KEY_NAME_HASH, tx->register_address.name_hash, GENERIC_HASH_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  result = add_hex_fixed(
      writer, GRDM_JSON_KEY_ACCOUNT_PUBLIC_KEY, tx->register_address.account_public_key,
      SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  arnm_json_writer_add_string(
      writer, GRDM_JSON_KEY_ADDRESS_TYPE, grdt_address_to_string(tx->address_type)
  );
  arnm_json_writer_add_uint64(writer, GRDM_JSON_KEY_DERIVATION_INDEX, tx->derivation_index);
  arnm_json_writer_close(writer);
  return ARNM_SUCCESS;
}

/** @brief Write the community-root branch: the community key and its two account keys. */
static arnm_result add_community_root(
    arnm_json_writer *writer, const grdr_complete_transaction *tx
) {
  arnm_json_writer_open_object(writer, GRDM_JSON_KEY_COMMUNITY_ROOT);
  arnm_result result = add_hex_fixed(
      writer, GRDM_JSON_KEY_PUBLIC_KEY, tx->community_root.public_key, SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  result = add_hex_fixed(
      writer, GRDM_JSON_KEY_GMW_PUBLIC_KEY, tx->community_root.gmw_public_key, SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  result = add_hex_fixed(
      writer, GRDM_JSON_KEY_AUF_PUBLIC_KEY, tx->community_root.auf_public_key, SIGN_PUBLIC_KEY_SIZE
  );
  if (ARNM_SUCCESS != result) { return result; }
  arnm_json_writer_close(writer);
  return ARNM_SUCCESS;
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
  arnm_result result = ARNM_SUCCESS;
  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_TRANSFER:
    result = add_transfer(writer, tx);
    break;
  case GRDT_TRANSACTION_CREATION:
    result = add_transfer(writer, tx);
    if (ARNM_SUCCESS != result) { break; }
    arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_TARGET_DATE, tx->target_date);
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    result = add_register_address(writer, tx);
    break;
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
    result = add_transfer(writer, tx);
    if (ARNM_SUCCESS != result) { break; }
    arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_TIMEOUT_DURATION, tx->timeout_duration);
    break;
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    result = add_transfer(writer, tx);
    if (ARNM_SUCCESS != result) { break; }
    arnm_json_writer_add_uint64(writer, GRDM_JSON_KEY_PREVIOUS_TX, tx->previous_tx);
    break;
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER:
    arnm_json_writer_add_uint64(writer, GRDM_JSON_KEY_PREVIOUS_TX, tx->previous_tx);
    break;
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    result = add_community_root(writer, tx);
    break;
  default:
    result = ARNM_ERROR_ENUM_UNHANDLED;
    break;
  }
  return result;
}

/**
 * @brief Write the three arrays: balances as they settled, memos, and the signatures over them.
 *
 * Each is written even when it holds nothing, so an empty array and an absent member never
 * have to mean the same thing to whoever reads the document next.
 */
static arnm_result add_arrays(
    arnm_json_writer *writer, arnm *allocator, const grdr_complete_transaction *tx
) {
  arnm_result result = ARNM_SUCCESS;

  arnm_json_writer_open_array(writer, GRDM_JSON_KEY_ACCOUNT_BALANCES);
  for (size_t i = 0; i < tx->account_balances_count; ++i) {
    const grdw_account_balance *balance = &tx->account_balances[i];
    arnm_json_writer_open_object(writer, NULL);
    result = add_hex_fixed(writer, GRDM_JSON_KEY_PUBKEY, balance->pubkey, SIGN_PUBLIC_KEY_SIZE);
    if (ARNM_SUCCESS != result) { return result; }
    arnm_json_writer_add_int64(writer, GRDM_JSON_KEY_BALANCE, balance->balance);
    add_uuid(writer, GRDM_JSON_KEY_COMMUNITY_UUID, balance->community_uuid);
    arnm_json_writer_close(writer);
  }
  arnm_json_writer_close(writer);

  arnm_json_writer_open_array(writer, GRDM_JSON_KEY_ENCRYPTED_MEMOS);
  for (size_t i = 0; i < tx->encrypted_memos_count; ++i) {
    const grdw_encrypted_memo *memo = &tx->encrypted_memos[i];
    arnm_json_writer_open_object(writer, NULL);
    arnm_json_writer_add_string(writer, GRDM_JSON_KEY_TYPE, grdt_memo_key_to_string(memo->type));
    result = add_hex_block(writer, allocator, GRDM_JSON_KEY_MEMO, &memo->memo);
    if (ARNM_SUCCESS != result) { return result; }
    arnm_json_writer_close(writer);
  }
  arnm_json_writer_close(writer);

  arnm_json_writer_open_array(writer, GRDM_JSON_KEY_SIGNATURE_PAIRS);
  for (size_t i = 0; i < tx->signature_pairs_count; ++i) {
    const grdw_signature_pair *pair = &tx->signature_pairs[i];
    arnm_json_writer_open_object(writer, NULL);
    result =
        add_hex_fixed(writer, GRDM_JSON_KEY_PUBLIC_KEY, pair->public_key, SIGN_PUBLIC_KEY_SIZE);
    if (ARNM_SUCCESS != result) { return result; }
    result = add_hex_fixed(writer, GRDM_JSON_KEY_SIGNATURE, pair->signature, SIGN_SIGNATURE_SIZE);
    if (ARNM_SUCCESS != result) { return result; }
    arnm_json_writer_close(writer);
  }
  arnm_json_writer_close(writer);

  return result;
}

/** @brief Lay the whole transaction into the writer, in the order the struct declares it. */
static arnm_result add_complete_transaction(
    arnm_json_writer *writer, arnm *allocator, const grdr_complete_transaction *tx
) {
  arnm_json_writer_begin_object(writer);

  arnm_json_writer_add_uint64(writer, GRDM_JSON_KEY_TX_NR, tx->tx_nr);
  add_timestamp(writer, GRDM_JSON_KEY_CONFIRMED_AT, &tx->confirmed_at);
  add_timestamp(writer, GRDM_JSON_KEY_CREATED_AT, &tx->created_at);
  add_uuid(writer, GRDM_JSON_KEY_TX_COMMUNITY_UUID, tx->tx_community_uuid);
  add_ledger_anchor(writer, GRDM_JSON_KEY_LEDGER_ANCHOR, &tx->ledger_anchor);

  arnm_json_writer_add_string(
      writer, GRDM_JSON_KEY_TRANSACTION_TYPE, grdt_transaction_to_string(tx->transaction_type)
  );
  arnm_json_writer_add_string(
      writer, GRDM_JSON_KEY_BALANCE_DERIVATION_TYPE,
      grdt_balance_derivation_to_string(tx->balance_derivation_type)
  );
  arnm_json_writer_add_string(
      writer, GRDM_JSON_KEY_CROSS_GROUP_TYPE, grdt_cross_group_to_string(tx->cross_group_type)
  );

  arnm_result result =
      add_hex_fixed(writer, GRDM_JSON_KEY_TX_RUNNING_HASH, tx->tx_running_hash, GENERIC_HASH_SIZE);
  if (ARNM_SUCCESS != result) { return result; }

  result = add_transaction_detail(writer, tx);
  if (ARNM_SUCCESS != result) { return result; }

  result = add_arrays(writer, allocator, tx);
  if (ARNM_SUCCESS != result) { return result; }

  // the two cross-group members are written only where they are set: on a local transaction
  // they are NULL, and a document that carried them as null would say something the wire never
  // said
  if (tx->tx_pairing_community_uuid) {
    add_uuid(writer, GRDM_JSON_KEY_TX_PAIRING_COMMUNITY_UUID, tx->tx_pairing_community_uuid);
  }
  if (tx->pairing_ledger_anchor) {
    add_ledger_anchor(writer, GRDM_JSON_KEY_PAIRING_LEDGER_ANCHOR, tx->pairing_ledger_anchor);
  }

  return add_hex_block(writer, allocator, GRDM_JSON_KEY_BODY_BYTES, &tx->body_bytes);
}

arnm_result grdm_json_from_complete_transaction(
    arnm_memory_block *out,
    const grdr_complete_transaction *tx,
    arnm *allocator,
    arnm_json_write_flags flags
) {
  if (!out || !tx) { return ARNM_ERROR_NULL_POINTER; }

  arnm_json_writer writer;
  arnm_result result = arnm_json_writer_init(&writer, allocator, flags);
  if (ARNM_SUCCESS != result) { return result; }

  result = add_complete_transaction(&writer, allocator, tx);
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
