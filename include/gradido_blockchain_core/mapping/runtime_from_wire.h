#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_RUNTIME_FROM_WIRE_H
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_RUNTIME_FROM_WIRE_H

// make sure, that generated protobuf enum is identical with grdw enum
#include "gradido_blockchain_core/result.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// forward declarations from gradido data wire
typedef struct grdr_complete_transaction grdr_complete_transaction;
typedef struct grdw_confirmed_transaction grdw_confirmed_transaction;
typedef struct grdw_transaction_body grdw_transaction_body;

grd_result grdm_complete_transaction_from_wire(
    grdr_complete_transaction *tx,
    const grdw_transaction_body *body,
    const grdw_confirmed_transaction *confirmed_tx,
    const uint8_t community_uuid[16]
);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_RUNTIME_FROM_WIRE_H
