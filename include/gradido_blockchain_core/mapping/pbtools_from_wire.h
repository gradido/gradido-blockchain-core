#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_PBTOOLS_FROM_WIRE_H
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_PBTOOLS_FROM_WIRE_H

// make sure, that generated protobuf enum is identical with grdw enum
#include "address_pb_compat.h"
#include "balance_derivation_pb_compat.h"
#include "cross_group_pb_compat.h"
#include "ledger_anchor_pb_compat.h"
#include "memo_key_pb_compat.h"
#include "gradido_blockchain_core/result.h"

#ifdef __cplusplus
extern "C" {
#endif

// forward declarations from pbtools
struct proto_gradido_confirmed_transaction_t;
struct proto_gradido_gradido_transaction_t;
struct proto_gradido_transaction_body_t;

// forward declarations from gradido data wire
typedef struct grdw_confirmed_transaction grdw_confirmed_transaction;
typedef struct grdw_gradido_transaction grdw_gradido_transaction;
typedef struct grdw_transaction_body grdw_transaction_body;

typedef struct grd_memory grd_memory;

grd_result grdm_transaction_body_to_pbtools(
    struct proto_gradido_transaction_body_t* pb_transaction_body,
    const grdw_transaction_body* transaction_body,
    grd_memory* allocator
);

grd_result grdm_gradido_transaction_to_pb(
    struct proto_gradido_gradido_transaction_t* pbtx,
    const grdw_gradido_transaction* tx,
    grd_memory* allocator
);

grd_result grdm_confirmed_transaction_to_pb(
    struct proto_gradido_confirmed_transaction_t* pb_confirmed_tx,
    const grdw_confirmed_transaction* confirmed_tx,
    grd_memory* allocator
);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_PBTOOLS_FROM_WIRE_H
