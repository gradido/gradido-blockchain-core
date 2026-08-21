#include "gradido_blockchain_core/mapping/json_from_wire.h"

#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"
#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/data/wire/specific_transactions.h"
#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "json_writer.c"

#include "gradido_blockchain_core/types/address.h"
#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_blockchain_core/types/cross_group.h"
#include "gradido_blockchain_core/types/transaction.h"

#include <stdbool.h>

// ****************** payload pieces ********************************************************

/**
 * @brief `grdw_transfer_amount` as `{ "pubkey": ..., "amount": ..., "community_uuid": ... }`.
 *
 * The wire carries who, how much and whose coins as one field, and the text keeps them
 * together. The runtime view spreads the same three across the transaction, which is the one
 * place the two views of a transfer differ in shape rather than in wording.
 */
static yyjson_mut_val *transfer_amount_obj(
    grdm_json_writer *writer, const grdw_transfer_amount *amount
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *pubkey = grdm_json_hex(writer, amount->pubkey, SIGN_PUBLIC_KEY_SIZE);
  yyjson_mut_val *value = grdm_json_unit(writer, amount->amount);
  yyjson_mut_val *uuid = grdm_json_uuid(writer, amount->community_uuid);
  if (!obj || !pubkey || !value || !uuid) { return NULL; }

  bool ok = yyjson_mut_obj_add_val(doc, obj, "pubkey", pubkey);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "amount", value);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "community_uuid", uuid);
  return ok ? obj : NULL;
}

/** @brief `{ "sender": { ... }, "recipient": ... }`, the sender an amount, the recipient a key. */
static yyjson_mut_val *transfer_obj(
    grdm_json_writer *writer, const grdw_gradido_transfer *transfer
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *sender = transfer_amount_obj(writer, &transfer->sender);
  yyjson_mut_val *recipient = grdm_json_hex(writer, transfer->recipient, SIGN_PUBLIC_KEY_SIZE);
  if (!obj || !sender || !recipient) { return NULL; }

  bool ok = yyjson_mut_obj_add_val(doc, obj, "sender", sender);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "recipient", recipient);
  return ok ? obj : NULL;
}

static yyjson_mut_val *creation_obj(
    grdm_json_writer *writer, const grdw_gradido_creation *creation
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *recipient = transfer_amount_obj(writer, &creation->recipient);
  if (!obj || !recipient) { return NULL; }

  bool ok = yyjson_mut_obj_add_val(doc, obj, "recipient", recipient);
  // whole seconds, and a month anchor rather than a moment -- a number, not a timestamp string
  ok = ok && yyjson_mut_obj_add_sint(doc, obj, "target_date", creation->target_date.seconds);
  return ok ? obj : NULL;
}

static yyjson_mut_val *register_address_obj(
    grdm_json_writer *writer, const grdw_register_address *address
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *user = grdm_json_hex(writer, address->user_pubkey, SIGN_PUBLIC_KEY_SIZE);
  yyjson_mut_val *name_hash = grdm_json_hex(writer, address->name_hash, GENERIC_HASH_SIZE);
  yyjson_mut_val *account = grdm_json_hex(writer, address->account_pubkey, SIGN_PUBLIC_KEY_SIZE);
  if (!obj || !user || !name_hash || !account) { return NULL; }

  bool ok = yyjson_mut_obj_add_val(doc, obj, "user_pubkey", user);
  ok = ok && yyjson_mut_obj_add_str(
                 doc, obj, "address_type", grdt_address_to_string(address->address_type)
             );
  ok = ok && yyjson_mut_obj_add_uint(doc, obj, "derivation_index", address->derivation_index);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "name_hash", name_hash);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "account_pubkey", account);
  return ok ? obj : NULL;
}

static yyjson_mut_val *community_root_obj(
    grdm_json_writer *writer, const grdw_community_root *root
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *pubkey = grdm_json_hex(writer, root->pubkey, SIGN_PUBLIC_KEY_SIZE);
  yyjson_mut_val *gmw = grdm_json_hex(writer, root->gmw_pubkey, SIGN_PUBLIC_KEY_SIZE);
  yyjson_mut_val *auf = grdm_json_hex(writer, root->auf_pubkey, SIGN_PUBLIC_KEY_SIZE);
  if (!obj || !pubkey || !gmw || !auf) { return NULL; }

  bool ok = yyjson_mut_obj_add_val(doc, obj, "pubkey", pubkey);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "gmw_pubkey", gmw);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "auf_pubkey", auf);
  return ok ? obj : NULL;
}

static yyjson_mut_val *deferred_transfer_obj(
    grdm_json_writer *writer, const grdw_gradido_deferred_transfer *deferred
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *transfer = transfer_obj(writer, &deferred->transfer);
  if (!obj || !transfer) { return NULL; }

  bool ok = yyjson_mut_obj_add_val(doc, obj, "transfer", transfer);
  ok = ok && yyjson_mut_obj_add_uint(doc, obj, "timeout_duration", deferred->timeout_duration);
  return ok ? obj : NULL;
}

static yyjson_mut_val *redeem_deferred_transfer_obj(
    grdm_json_writer *writer, const grdw_gradido_redeem_deferred_transfer *redeem
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_val *transfer = transfer_obj(writer, &redeem->transfer);
  if (!obj || !transfer) { return NULL; }

  bool ok = yyjson_mut_obj_add_uint(
      doc, obj, "deferred_transfer_transaction_nr", redeem->deferred_transfer_transaction_nr
  );
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "transfer", transfer);
  return ok ? obj : NULL;
}

// ****************** the body **************************************************************

/**
 * @brief The payload under the name of the union member the transaction type selects.
 *
 * Every branch of the wire union is described, the community friends update included -- this
 * mapping renders what arrived, and what the runtime can make of it is a different question,
 * settled in runtime_from_wire.c.
 *
 * @param[out] ok Set to false when a value could not be built; left alone otherwise, so the
 *                caller separates "this type names no payload" from "this ran out of memory".
 * @return false when @p body carries GRDT_TRANSACTION_NONE or a value outside the enum.
 */
static bool add_body_payload(
    grdm_json_writer *writer, yyjson_mut_val *root, const grdw_transaction_body *body, bool *ok
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *payload = NULL;
  const char *key = NULL;

  // sorted by expected frequency of occurrence, as in runtime_from_wire.c
  switch (body->transaction_type) {
  case GRDT_TRANSACTION_TRANSFER:
    key = "transfer";
    payload = transfer_obj(writer, &body->transfer);
    break;
  case GRDT_TRANSACTION_CREATION:
    key = "creation";
    payload = creation_obj(writer, &body->creation);
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    key = "register_address";
    payload = register_address_obj(writer, &body->register_address);
    break;
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
    key = "deferred_transfer";
    payload = deferred_transfer_obj(writer, &body->deferred_transfer);
    break;
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    key = "redeem_deferred_transfer";
    payload = redeem_deferred_transfer_obj(writer, &body->redeem_deferred_transfer);
    break;
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER:
    key = "timeout_deferred_transfer";
    payload = yyjson_mut_obj(doc);
    *ok = payload && yyjson_mut_obj_add_uint(
                         doc, payload, "deferred_transfer_transaction_nr",
                         body->timeout_deferred_transfer.deferred_transfer_transaction_nr
                     );
    if (!*ok) { return true; }
    break;
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    key = "community_root";
    payload = community_root_obj(writer, &body->community_root);
    break;
  case GRDT_TRANSACTION_COMMUNITY_FRIENDS_UPDATE:
    key = "community_friends_update";
    payload = yyjson_mut_obj(doc);
    *ok = payload && yyjson_mut_obj_add_bool(
                         doc, payload, "color_fusion", body->community_friends_update.color_fusion
                     );
    if (!*ok) { return true; }
    break;
  default:
    // GRDT_TRANSACTION_NONE and anything outside the enum name no union member, so there is
    // nothing to read and nothing to write
    return false;
  }

  *ok = payload && yyjson_mut_obj_add_val(doc, root, key, payload);
  return true;
}

static hostmem_result build_body(
    grdm_json_writer *writer, yyjson_mut_val *root, const grdw_transaction_body *body
) {
  yyjson_mut_doc *doc = writer->doc;

  yyjson_mut_val *created_at = grdm_json_timestamp(writer, &body->created_at);
  if (!created_at) { return grdm_json_failure(writer); }

  bool ok = yyjson_mut_obj_add_str(
      doc, root, "transaction_type", grdt_transaction_to_string(body->transaction_type)
  );
  // the struct calls this member `type`; next to `transaction_type` in the text that would read
  // as a second name for the same thing, so it goes out under the name the runtime view uses
  ok =
      ok &&
      yyjson_mut_obj_add_str(doc, root, "cross_group_type", grdt_cross_group_to_string(body->type));
  ok = ok && yyjson_mut_obj_add_val(doc, root, "created_at", created_at);
  if (!ok) { return grdm_json_failure(writer); }

  // only a cross group body names another community
  if (body->other_community_uuid) {
    yyjson_mut_val *other = grdm_json_uuid(writer, body->other_community_uuid);
    if (!other || !yyjson_mut_obj_add_val(doc, root, "other_community_uuid", other)) {
      return grdm_json_failure(writer);
    }
  }

  if (!add_body_payload(writer, root, body, &ok)) { return HOSTMEM_ERROR_ENUM_UNHANDLED; }
  if (!ok) { return grdm_json_failure(writer); }

  if (!grdm_json_add_encrypted_memos(writer, root, "memos", body->memos, body->memos_count)) {
    return grdm_json_failure(writer);
  }
  return HOSTMEM_SUCCESS;
}

// ****************** the gradido transaction ***********************************************

/**
 * @brief The decoded body under `body`, with the workspace carved out of the render's own chain.
 *
 * The two allocators a render already has are the two the decode needs, so no third one is
 * introduced: @p pb_workspace_size bytes are reserved from the work chain and handed to pbtools,
 * and the wire structure the decode builds is placed in the same chain beside them. Everything
 * goes away at the caller's reset together with the document.
 *
 * The size is the caller's, all the way down. A stretch too small for the body comes back as
 * HOSTMEM_ERROR_OUT_OF_MEMORY with nothing else wrong, which is what lets a caller reading a
 * stream raise the figure once and keep it.
 */
static hostmem_result add_decoded_body(
    grdm_json_writer *writer,
    yyjson_mut_val *root,
    const grdw_gradido_transaction *tx,
    uint32_t pb_workspace_size
) {
  yyjson_mut_doc *doc = writer->doc;
  if (!pb_workspace_size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  hostmem_memory_block workspace = {NULL, 0};
  hostmem_result taken = grdw_block_alloc(&workspace, pb_workspace_size, writer->work);
  if (HOSTMEM_SUCCESS != taken) { return taken; }

  // initialised first: the decode writes only the members the message carries, and the optional
  // ones -- other_community_uuid above all -- are read below as "set or not set"
  grdw_transaction_body decoded;
  grdw_transaction_body_init(&decoded);
  hostmem_result read =
      grdw_transaction_body_decode(&decoded, &tx->body_bytes, &workspace, writer->work);
  if (HOSTMEM_SUCCESS != read) { return read; }

  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  if (!obj) { return grdm_json_failure(writer); }
  hostmem_result built = build_body(writer, obj, &decoded);
  if (HOSTMEM_SUCCESS != built) { return built; }
  // under `body`, not `body_bytes`: the member name is what tells a reader which of the two
  // they are looking at
  if (!yyjson_mut_obj_add_val(doc, root, "body", obj)) { return grdm_json_failure(writer); }
  return HOSTMEM_SUCCESS;
}

/** @brief The body as it arrived, hex under `body_bytes`. */
static hostmem_result add_body_bytes(
    grdm_json_writer *writer, yyjson_mut_val *root, const grdw_gradido_transaction *tx
) {
  yyjson_mut_val *body = grdm_json_hex(writer, tx->body_bytes.data, tx->body_bytes.size);
  if (!body || !yyjson_mut_obj_add_val(writer->doc, root, "body_bytes", body)) {
    return grdm_json_failure(writer);
  }
  return HOSTMEM_SUCCESS;
}

/**
 * @param[in] pb_workspace_size 0 leaves the body as hex; anything else decodes it with a stretch
 *                              of that many bytes. The two public entry points differ in nothing
 *                              else, which is why they meet here.
 */
static hostmem_result build_gradido_transaction(
    grdm_json_writer *writer,
    yyjson_mut_val *root,
    const grdw_gradido_transaction *tx,
    uint32_t pb_workspace_size
) {
  yyjson_mut_doc *doc = writer->doc;

  if (!grdm_json_add_signature_pairs(writer, root, "sig_map", tx->sig_map, tx->sig_map_count)) {
    return grdm_json_failure(writer);
  }

  // no bytes, nothing to say about them -- in either shape
  if (tx->body_bytes.size) {
    hostmem_result body = pb_workspace_size ? add_decoded_body(writer, root, tx, pb_workspace_size)
                                            : add_body_bytes(writer, root, tx);
    if (HOSTMEM_SUCCESS != body) { return body; }
  }

  // a local transaction pairs with nothing, and the unspecified anchor stays out rather than
  // appearing as a type with no value under it
  if (GRDT_LEDGER_ANCHOR_UNSPECIFIED != tx->pairing_ledger_anchor.type) {
    yyjson_mut_val *anchor = grdm_json_ledger_anchor(writer, &tx->pairing_ledger_anchor);
    if (!anchor || !yyjson_mut_obj_add_val(doc, root, "pairing_ledger_anchor", anchor)) {
      return grdm_json_failure(writer);
    }
  }
  return HOSTMEM_SUCCESS;
}

// ****************** the confirmed transaction *********************************************

static hostmem_result build_confirmed_transaction(
    grdm_json_writer *writer,
    yyjson_mut_val *root,
    const grdw_confirmed_transaction *tx,
    uint32_t pb_workspace_size
) {
  yyjson_mut_doc *doc = writer->doc;

  yyjson_mut_val *confirmed_at = grdm_json_timestamp(writer, &tx->confirmed_at);
  yyjson_mut_val *running_hash = grdm_json_hex(writer, tx->running_hash, GENERIC_HASH_SIZE);
  yyjson_mut_val *anchor = grdm_json_ledger_anchor(writer, &tx->ledger_anchor);
  yyjson_mut_val *nested = yyjson_mut_obj(doc);
  if (!confirmed_at || !running_hash || !anchor || !nested) { return grdm_json_failure(writer); }

  // the nested transaction is built into its own object and hung under "transaction", which is
  // the member it occupies in the struct
  hostmem_result built =
      build_gradido_transaction(writer, nested, &tx->transaction, pb_workspace_size);
  if (HOSTMEM_SUCCESS != built) { return built; }

  bool ok = yyjson_mut_obj_add_uint(doc, root, "id", tx->id);
  ok = ok && yyjson_mut_obj_add_val(doc, root, "transaction", nested);
  ok = ok && yyjson_mut_obj_add_val(doc, root, "confirmed_at", confirmed_at);
  ok = ok && yyjson_mut_obj_add_val(doc, root, "running_hash", running_hash);
  ok = ok && yyjson_mut_obj_add_val(doc, root, "ledger_anchor", anchor);
  ok = ok && yyjson_mut_obj_add_str(
                 doc, root, "balance_derivation",
                 grdt_balance_derivation_to_string(tx->balance_derivation)
             );
  if (!ok) { return grdm_json_failure(writer); }

  if (!grdm_json_add_account_balances(
          writer, root, "account_balances", tx->account_balances, tx->account_balances_count
      )) {
    return grdm_json_failure(writer);
  }
  return HOSTMEM_SUCCESS;
}

// ****************** the three entry points ************************************************

hostmem_result grdm_transaction_body_to_json(
    hostmem_memory_block *json,
    const grdw_transaction_body *body,
    grdm_json_format format,
    hostmem_multi_arena *work,
    hostmem_multi_arena *result
) {
  if (!json || !body || !work || !result) { return HOSTMEM_ERROR_NULL_POINTER; }

  grdm_json_writer writer;
  yyjson_mut_val *root = NULL;
  hostmem_result opened = grdm_json_writer_begin(&writer, &root, work);
  if (HOSTMEM_SUCCESS != opened) { return opened; }

  hostmem_result built = build_body(&writer, root, body);
  if (HOSTMEM_SUCCESS != built) { return built; }

  return grdm_json_writer_finish(json, &writer, format, result);
}

hostmem_result grdm_gradido_transaction_to_json(
    hostmem_memory_block *json,
    const grdw_gradido_transaction *tx,
    grdm_json_format format,
    hostmem_multi_arena *work,
    hostmem_multi_arena *result
) {
  if (!json || !tx || !work || !result) { return HOSTMEM_ERROR_NULL_POINTER; }

  grdm_json_writer writer;
  yyjson_mut_val *root = NULL;
  hostmem_result opened = grdm_json_writer_begin(&writer, &root, work);
  if (HOSTMEM_SUCCESS != opened) { return opened; }

  // 0: no decode, the body stays the bytes it arrived as
  hostmem_result built = build_gradido_transaction(&writer, root, tx, 0);
  if (HOSTMEM_SUCCESS != built) { return built; }

  return grdm_json_writer_finish(json, &writer, format, result);
}

hostmem_result grdm_gradido_transaction_with_body_to_json(
    hostmem_memory_block *json,
    const grdw_gradido_transaction *tx,
    grdm_json_format format,
    uint32_t pb_workspace_size,
    hostmem_multi_arena *work,
    hostmem_multi_arena *result
) {
  if (!json || !tx || !work || !result) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!pb_workspace_size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  grdm_json_writer writer;
  yyjson_mut_val *root = NULL;
  hostmem_result opened = grdm_json_writer_begin(&writer, &root, work);
  if (HOSTMEM_SUCCESS != opened) { return opened; }

  hostmem_result built = build_gradido_transaction(&writer, root, tx, pb_workspace_size);
  if (HOSTMEM_SUCCESS != built) { return built; }

  return grdm_json_writer_finish(json, &writer, format, result);
}

hostmem_result grdm_confirmed_transaction_to_json(
    hostmem_memory_block *json,
    const grdw_confirmed_transaction *tx,
    grdm_json_format format,
    hostmem_multi_arena *work,
    hostmem_multi_arena *result
) {
  if (!json || !tx || !work || !result) { return HOSTMEM_ERROR_NULL_POINTER; }

  grdm_json_writer writer;
  yyjson_mut_val *root = NULL;
  hostmem_result opened = grdm_json_writer_begin(&writer, &root, work);
  if (HOSTMEM_SUCCESS != opened) { return opened; }

  hostmem_result built = build_confirmed_transaction(&writer, root, tx, 0);
  if (HOSTMEM_SUCCESS != built) { return built; }

  return grdm_json_writer_finish(json, &writer, format, result);
}

hostmem_result grdm_confirmed_transaction_with_body_to_json(
    hostmem_memory_block *json,
    const grdw_confirmed_transaction *tx,
    grdm_json_format format,
    uint32_t pb_workspace_size,
    hostmem_multi_arena *work,
    hostmem_multi_arena *result
) {
  if (!json || !tx || !work || !result) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!pb_workspace_size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  grdm_json_writer writer;
  yyjson_mut_val *root = NULL;
  hostmem_result opened = grdm_json_writer_begin(&writer, &root, work);
  if (HOSTMEM_SUCCESS != opened) { return opened; }

  hostmem_result built = build_confirmed_transaction(&writer, root, tx, pb_workspace_size);
  if (HOSTMEM_SUCCESS != built) { return built; }

  return grdm_json_writer_finish(json, &writer, format, result);
}
