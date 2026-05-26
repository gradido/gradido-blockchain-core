#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_CONFIRMED_TRANSACTION_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_CONFIRMED_TRANSACTION_H

#include "basic_types.h"
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_transaction.h"
#include "ledger_anchor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grdw_confirmed_transaction {
  uint64_t id;
  grdw_gradido_transaction transaction;
  grdd_timestamp confirmed_at;
  uint8_t running_hash[32];
  grdw_ledger_anchor ledger_anchor;
  grdw_account_balance *account_balances;
  uint8_t account_balances_count;
  grdt_balance_derivation balance_derivation;
} grdw_confirmed_transaction;

void grdw_confirmed_transaction_init(grdw_confirmed_transaction *tx);

grd_result grdw_confirmed_transaction_reserve_account_balances(
    grdw_confirmed_transaction *tx, uint8_t account_balances_count, grd_memory *allocator
);

grd_result grdw_confirmed_transaction_copy_account_balance(
    grdw_confirmed_transaction *tx, grdw_account_balance *account_balance, uint8_t index
);

grd_result grdw_confirmed_transaction_decode(
    grdw_confirmed_transaction *tx, const grd_memory_block *binarySrc, grd_memory *allocator
);

grd_result grdw_confirmed_transaction_encode(
    grd_memory_block *binaryDst,
    size_t *final_size,
    const grdw_confirmed_transaction *tx,
    grd_memory *allocator
);

void grdw_confirmed_transaction_free(grdw_confirmed_transaction *tx, grd_memory *allocator);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_CONFIRMED_TRANSACTION_H
