
#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_LEDGER_ANCHOR_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_LEDGER_ANCHOR_H

#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/types/ledger_anchor.h"
#include "hiero.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grdw_ledger_anchor {
  grdt_ledger_anchor type;
  union {
    grdw_hiero_transaction_id hiero_transaction_id;
    uint64_t id;
  };
} grdw_ledger_anchor;

grd_result grdw_ledger_anchor_set_hiero_transaction_id(
    grdw_ledger_anchor *ledger_anchor,
    grdd_timestamp transaction_valid_start,
    int64_t account_id_shard_num,
    int64_t account_id_realm_num,
    int64_t account_id_account_num
);

grd_result grdw_ledger_anchor_set_legacy_id(
    grdw_ledger_anchor *ledger_anchor, grdt_ledger_anchor type, uint64_t legacy_id
);

grd_result grdw_ledger_anchor_set_node_trigger_transaction_id(
    grdw_ledger_anchor *ledger_anchor, uint64_t node_trigger_transaction_id
);

#ifdef __cplusplus
}
#endif

#endif /* GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_LEDGER_ANCHOR_H */
