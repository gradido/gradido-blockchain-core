#include "gradido_blockchain_core/data/wire/ledger_anchor.h"

grd_result grdw_ledger_anchor_set_hiero_transaction_id(
    grdw_ledger_anchor *ledger_anchor,
    grdw_timestamp transaction_valid_start,
    int64_t account_id_shard_num,
    int64_t account_id_realm_num,
    int64_t account_id_account_num
) {
  if (!ledger_anchor) { return GRD_ERROR_NULL_POINTER; }
  ledger_anchor->type = GRDW_LEDGER_ANCHOR_TYPE_HIERO_TRANSACTION_ID;
  ledger_anchor->hiero_transaction_id = (grdw_hiero_transaction_id){
      .transactionValidStart = transaction_valid_start,
      .accountID = (grdw_hiero_account_id){
          .shardNum = account_id_shard_num,
          .realmNum = account_id_realm_num,
          .accountNum = account_id_account_num
      }
  };
  return GRD_SUCCESS;
}

grd_result grdw_ledger_anchor_set_legacy_id(
    grdw_ledger_anchor *ledger_anchor, grdw_ledger_anchor_type type, uint64_t legacy_id
) {
  if (!ledger_anchor) { return GRD_ERROR_NULL_POINTER; }
  if (GRDW_LEDGER_ANCHOR_TYPE_UNSPECIFIED == type ||
      GRDW_LEDGER_ANCHOR_TYPE_HIERO_TRANSACTION_ID == type ||
      GRDW_LEDGER_ANCHOR_TYPE_NODE_TRIGGER_TRANSACTION_ID == type) {
    return GRD_ERROR_INVALID_ENUM_TYPE;
  }
  ledger_anchor->type = type;
  ledger_anchor->id = legacy_id;

  return GRD_SUCCESS;
}

grd_result grdw_ledger_anchor_set_node_trigger_transaction_id(
    grdw_ledger_anchor *ledger_anchor, uint64_t node_trigger_transaction_id
) {
  if (!ledger_anchor) { return GRD_ERROR_NULL_POINTER; }
  ledger_anchor->type = GRDW_LEDGER_ANCHOR_TYPE_NODE_TRIGGER_TRANSACTION_ID;
  ledger_anchor->id = node_trigger_transaction_id;

  return GRD_SUCCESS;
}
