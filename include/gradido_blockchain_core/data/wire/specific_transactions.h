#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_SPECIFIC_TRANSACTIONS_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_SPECIFIC_TRANSACTIONS_H

#include <stdbool.h>

#include "basic_types.h"
#include "gradido_blockchain_core/data/address_type.h"
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"
#include "hiero.h"
#include "ledger_anchor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grdw_community_friends_update {
    bool color_fusion;
} grdw_community_friends_update;

typedef struct grdw_community_root {
    uint8_t pubkey[32];
    uint8_t gmw_pubkey[32];
    uint8_t auf_pubkey[32];
} grdw_community_root;

typedef struct grdw_gradido_creation {
    grdw_transfer_amount recipient;
    grdw_timestamp_seconds target_date;
} grdw_gradido_creation;

typedef struct grdw_gradido_transfer {
    grdw_transfer_amount sender;
    uint8_t recipient[32];
} grdw_gradido_transfer;

typedef struct grdw_gradido_deferred_transfer {
    grdw_gradido_transfer transfer;
    uint32_t timeout_duration;
} grdw_gradido_deferred_transfer;

typedef struct grdw_gradido_redeem_deferred_transfer {
    uint64_t deferred_transfer_transaction_nr;
    grdw_gradido_transfer transfer;
} grdw_gradido_redeem_deferred_transfer;

typedef struct grdw_gradido_timeout_deferred_transfer {
    uint64_t deferred_transfer_transaction_nr;
} grdw_gradido_timeout_deferred_transfer;

typedef struct grdw_register_address {
    uint8_t user_pubkey[32];
    grdd_address_type address_type;
    uint32_t derivation_index;
    uint8_t name_hash[32];
    uint8_t account_pubkey[32];
} grdw_register_address;

#ifdef __cplusplus
}
#endif


#endif // GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_SPECIFIC_TRANSACTIONS_H
