#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_CONFIRMED_TRANSACTION_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_CONFIRMED_TRANSACTION_H

#include "basic_types.h"
#include "gradido_blockchain_core/data/balance_derivation_type.h"
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_transaction.h"
#include "ledger_anchor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grdw_confirmed_transaction {
    uint64_t id;
    grdw_gradido_transaction transaction;
    grdw_timestamp confirmed_at;
    uint8_t running_hash[32];
    grdw_ledger_anchor ledger_anchor;
    grdw_account_balance* account_balances;
    uint8_t account_balances_count;
    grdd_balance_derivation_type balance_derivation;
} grdw_confirmed_transaction;

grd_result grdw_confirmed_transaction_reserve_account_balances(
    grdw_confirmed_transaction* tx, uint8_t account_balances_count,
    grd_memory* allocator);

grd_result grdw_confirmed_transaction_copy_account_balance(
    grdw_confirmed_transaction* tx, grdw_account_balance* account_balance,
    uint8_t index);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_CONFIRMED_TRANSACTION_H
