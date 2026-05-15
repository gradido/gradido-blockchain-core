
#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_LEDGER_ANCHOR_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_LEDGER_ANCHOR_H

#include "gradido_blockchain_core/result.h"
#include "hiero.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GRDW_LEDGER_ANCHOR_TYPE_UNSPECIFIED = 0,
  GRDW_LEDGER_ANCHOR_TYPE_HIERO_TRANSACTION_ID = 2,
  GRDW_LEDGER_ANCHOR_TYPE_LEGACY_GRADIDO_DB_TRANSACTION_ID = 3,
  GRDW_LEDGER_ANCHOR_TYPE_NODE_TRIGGER_TRANSACTION_ID = 4,
  GRDW_LEDGER_ANCHOR_TYPE_LEGACY_GRADIDO_DB_COMMUNITY_ID = 5,
  GRDW_LEDGER_ANCHOR_TYPE_LEGACY_GRADIDO_DB_USER_ID = 6,
  GRDW_LEDGER_ANCHOR_TYPE_LEGACY_GRADIDO_DB_CONTRIBUTION_ID = 7,
  GRDW_LEDGER_ANCHOR_TYPE_LEGACY_GRADIDO_DB_TRANSACTION_LINK_ID = 8
} grdw_ledger_anchor_type;

typedef struct grdw_ledger_anchor {
  grdw_ledger_anchor_type type;
  union {
    grdw_hiero_transaction_id hiero_transaction_id;
    uint64_t legacy_id;
  };
} grdw_ledger_anchor;

#ifdef __cplusplus
}
#endif

#endif /* GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_LEDGER_ANCHOR_H */
