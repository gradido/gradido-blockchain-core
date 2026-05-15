#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"

#include <string.h>

grd_result grdw_confirmed_transaction_reserve_account_balances(
    grdw_confirmed_transaction *tx, uint8_t account_balances_count, grd_memory *allocator
) {
  if (!tx || !allocator) { return GRD_ERROR_NULL_POINTER; }
  if (!account_balances_count) { return GRD_ERROR_INVALID_PARAM; }
  grd_result result = grd_memory_buffer_alloc(
      (uint8_t **)&tx->account_balances, allocator,
      sizeof(grdw_account_balance) * account_balances_count
  );
  if (GRD_SUCCESS != result) { return result; }

  tx->account_balances_count = account_balances_count;
  return GRD_SUCCESS;
}

grd_result grdw_confirmed_transaction_copy_account_balance(
    grdw_confirmed_transaction *tx, grdw_account_balance *account_balance, uint8_t index
) {
  if (!tx || !account_balance) { return GRD_ERROR_NULL_POINTER; }
  if (index >= tx->account_balances_count) { return GRD_ERROR_INVALID_PARAM; }
  memcpy(&tx->account_balances[index], account_balance, index);
  return GRD_SUCCESS;
}
