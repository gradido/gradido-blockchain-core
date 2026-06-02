#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/types/cross_group.h"
#include <string.h>

void grdr_complete_transaction_init(grdr_complete_transaction *tx) {
  if (tx) { memset(tx, 0, sizeof(grdr_complete_transaction)); }
}

void grdr_complete_transaction_release(grdr_complete_transaction *tx) {
  if (tx) {
    grd_memory_free(&tx->memory_area);
    grdr_complete_transaction_init(tx);
  }
}

grd_result grdr_complete_transaction_get_sender_community_uuid(
    uint8_t *uuid, const grdr_complete_transaction *tx
) {
  if (!uuid || !tx) { return GRD_SUCCESS; }
  if (GRDT_CROSS_GROUP_LOCAL == tx->cross_group_type ||
      GRDT_CROSS_GROUP_OUTBOUND == tx->cross_group_type) {
    memcpy(uuid, tx->tx_community_uuid, 16);
    return GRD_SUCCESS;
  } else if (GRDT_CROSS_GROUP_INBOUND == tx->cross_group_type) {
    if (!tx->tx_pairing_community_uuid) { return GRD_ERROR_INVALID_STATE; }
    memcpy(uuid, tx->tx_pairing_community_uuid, 16);
    return GRD_SUCCESS;
  }
  return GRD_ERROR_NOT_IMPLEMENTED_YET;
}

grd_result grdr_complete_transaction_get_recipient_community_uuid(
    uint8_t *uuid, const grdr_complete_transaction *tx
) {
  if (!uuid || !tx) { return GRD_SUCCESS; }
  if (GRDT_CROSS_GROUP_LOCAL == tx->cross_group_type ||
      GRDT_CROSS_GROUP_INBOUND == tx->cross_group_type) {
    memcpy(uuid, tx->tx_community_uuid, 16);
    return GRD_SUCCESS;
  } else if (GRDT_CROSS_GROUP_OUTBOUND == tx->cross_group_type) {
    if (!tx->tx_pairing_community_uuid) { return GRD_ERROR_INVALID_STATE; }
    memcpy(uuid, tx->tx_pairing_community_uuid, 16);
    return GRD_SUCCESS;
  }
  return GRD_ERROR_NOT_IMPLEMENTED_YET;
}
