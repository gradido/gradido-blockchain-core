#include "gradido_blockchain_core/mapping/json_from_runtime.h"

#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "json_writer.c"

#include "gradido_blockchain_core/types/address.h"
#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_blockchain_core/types/cross_group.h"
#include "gradido_blockchain_core/types/transaction.h"

#include <stdbool.h>

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
    grdm_json_writer *writer,
    yyjson_mut_val *root,
    const grdr_complete_transaction *tx,
    bool with_sender
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *recipient =
      grdm_json_hex(writer, tx->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE);
  yyjson_mut_val *amount = grdm_json_unit(writer, tx->transfer.amount);
  if (!obj || !recipient || !amount) { return false; }

  bool ok = true;
  if (with_sender) {
    yyjson_mut_val *sender =
        grdm_json_hex(writer, tx->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE);
    ok = sender && yyjson_mut_obj_add_val(doc, obj, "sender_pubkey", sender);
  } else {
    ok = yyjson_mut_obj_add_null(doc, obj, "sender_pubkey");
  }
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "recipient_pubkey", recipient);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "amount", amount);
  // a creation carries no coin community of its own -- the gdd come into being in the
  // transaction's own community, which the root already names
  if (with_sender) {
    yyjson_mut_val *coin = grdm_json_uuid(writer, tx->transfer.coin_community_uuid);
    ok = ok && coin && yyjson_mut_obj_add_val(doc, obj, "coin_community_uuid", coin);
  }
  return ok && yyjson_mut_obj_add_val(doc, root, "transfer", obj);
}

static bool add_register_address(
    grdm_json_writer *writer, yyjson_mut_val *root, const grdr_complete_transaction *tx
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *user =
      grdm_json_hex(writer, tx->register_address.user_public_key, SIGN_PUBLIC_KEY_SIZE);
  yyjson_mut_val *name_hash =
      grdm_json_hex(writer, tx->register_address.name_hash, GENERIC_HASH_SIZE);
  yyjson_mut_val *account =
      grdm_json_hex(writer, tx->register_address.account_public_key, SIGN_PUBLIC_KEY_SIZE);
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
    grdm_json_writer *writer, yyjson_mut_val *root, const grdr_complete_transaction *tx
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *pubkey =
      grdm_json_hex(writer, tx->community_root.public_key, SIGN_PUBLIC_KEY_SIZE);
  yyjson_mut_val *gmw =
      grdm_json_hex(writer, tx->community_root.gmw_public_key, SIGN_PUBLIC_KEY_SIZE);
  yyjson_mut_val *auf =
      grdm_json_hex(writer, tx->community_root.auf_public_key, SIGN_PUBLIC_KEY_SIZE);
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
    grdm_json_writer *writer, yyjson_mut_val *root, const grdr_complete_transaction *tx, bool *ok
) {
  yyjson_mut_doc *doc = writer->doc;

  // sorted by expected frequency of occurrence, as in runtime_from_wire.c
  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_TRANSFER:
    *ok = add_transfer(writer, root, tx, true);
    break;
  case GRDT_TRANSACTION_CREATION:
    *ok = add_transfer(writer, root, tx, false);
    *ok = *ok && yyjson_mut_obj_add_sint(doc, root, "target_date", tx->target_date);
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    *ok = add_register_address(writer, root, tx);
    break;
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
    *ok = add_transfer(writer, root, tx, true);
    *ok = *ok && yyjson_mut_obj_add_sint(doc, root, "timeout_duration", tx->timeout_duration);
    break;
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    *ok = add_transfer(writer, root, tx, true);
    *ok = *ok && yyjson_mut_obj_add_uint(doc, root, "previous_tx", tx->previous_tx);
    break;
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER:
    // the detail union stays untouched for this type; the previous transaction is all it says
    *ok = yyjson_mut_obj_add_uint(doc, root, "previous_tx", tx->previous_tx);
    break;
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    *ok = add_community_root(writer, root, tx);
    break;
  default:
    return false;
  }
  return true;
}

// ****************** the whole transaction ***********************************************

static hostmem_result build_root(
    grdm_json_writer *writer, yyjson_mut_val *root, const grdr_complete_transaction *tx
) {
  yyjson_mut_doc *doc = writer->doc;

  yyjson_mut_val *created_at = grdm_json_timestamp(writer, &tx->created_at);
  yyjson_mut_val *confirmed_at = grdm_json_timestamp(writer, &tx->confirmed_at);
  yyjson_mut_val *community_uuid = grdm_json_uuid(writer, tx->tx_community_uuid);
  yyjson_mut_val *running_hash = grdm_json_hex(writer, tx->tx_running_hash, GENERIC_HASH_SIZE);
  yyjson_mut_val *anchor = grdm_json_ledger_anchor(writer, &tx->ledger_anchor);
  if (!created_at || !confirmed_at || !community_uuid || !running_hash || !anchor) {
    return grdm_json_failure(writer);
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
  if (!ok) { return grdm_json_failure(writer); }

  if (!add_transaction_detail(writer, root, tx, &ok)) { return HOSTMEM_ERROR_ENUM_UNHANDLED; }
  if (!ok) { return grdm_json_failure(writer); }

  if (!grdm_json_add_account_balances(
          writer, root, "account_balances", tx->account_balances, tx->account_balances_count
      )) {
    return grdm_json_failure(writer);
  }
  if (!grdm_json_add_encrypted_memos(
          writer, root, "encrypted_memos", tx->encrypted_memos, tx->encrypted_memos_count
      )) {
    return grdm_json_failure(writer);
  }
  if (!grdm_json_add_signature_pairs(
          writer, root, "signature_pairs", tx->signature_pairs, tx->signature_pairs_count
      )) {
    return grdm_json_failure(writer);
  }

  // the cross group members are NULL on a local transaction, and stay out of the text there
  if (tx->tx_pairing_community_uuid) {
    yyjson_mut_val *pairing_uuid = grdm_json_uuid(writer, tx->tx_pairing_community_uuid);
    if (!pairing_uuid ||
        !yyjson_mut_obj_add_val(doc, root, "tx_pairing_community_uuid", pairing_uuid)) {
      return grdm_json_failure(writer);
    }
  }
  if (tx->pairing_ledger_anchor) {
    yyjson_mut_val *pairing_anchor = grdm_json_ledger_anchor(writer, tx->pairing_ledger_anchor);
    if (!pairing_anchor ||
        !yyjson_mut_obj_add_val(doc, root, "pairing_ledger_anchor", pairing_anchor)) {
      return grdm_json_failure(writer);
    }
  }

  if (tx->body_bytes.size) {
    yyjson_mut_val *body = grdm_json_hex(writer, tx->body_bytes.data, tx->body_bytes.size);
    if (!body || !yyjson_mut_obj_add_val(doc, root, "body_bytes", body)) {
      return grdm_json_failure(writer);
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

  grdm_json_writer writer;
  yyjson_mut_val *root = NULL;
  hostmem_result opened = grdm_json_writer_begin(&writer, &root, work);
  if (HOSTMEM_SUCCESS != opened) { return opened; }

  hostmem_result built = build_root(&writer, root, tx);
  if (HOSTMEM_SUCCESS != built) { return built; }

  return grdm_json_writer_finish(json, &writer, format, result);
}
