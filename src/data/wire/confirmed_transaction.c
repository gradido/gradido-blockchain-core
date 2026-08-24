#include "pb_decode.h"

#include "arnm/arena.h"
#include "gradido_blockchain_core/data/proto/gradido/confirmed_transaction.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"
#include "gradido_blockchain_core/mapping/pbtools_from_wire.h"
#include "gradido_blockchain_core/mapping/wire_from_pbtools.h"
#include "gradido_blockchain_core/result.h"

#include <string.h>

void grdw_confirmed_transaction_init(grdw_confirmed_transaction *tx) {
  if (!tx) { return; }
  memset(tx, 0, sizeof(grdw_confirmed_transaction));
  grdw_gradido_transaction_init(&tx->transaction);
}

arnm_result grdw_confirmed_transaction_reserve_account_balances(
    grdw_confirmed_transaction *tx, uint8_t account_balances_count, arnm *allocator
) {
  if (!tx || !allocator) { return ARNM_ERROR_NULL_POINTER; }
  if (!account_balances_count) { return ARNM_ERROR_INVALID_PARAM; }
  arnm_result result = arnm_alloc(
      (uint8_t **)&tx->account_balances, sizeof(grdw_account_balance) * account_balances_count,
      allocator
  );
  if (ARNM_SUCCESS != result) { return result; }

  tx->account_balances_count = account_balances_count;
  return ARNM_SUCCESS;
}

arnm_result grdw_confirmed_transaction_copy_account_balance(
    grdw_confirmed_transaction *tx, grdw_account_balance *account_balance, uint8_t index
) {
  if (!tx || !account_balance) { return ARNM_ERROR_NULL_POINTER; }
  if (index >= tx->account_balances_count) { return ARNM_ERROR_INVALID_PARAM; }
  memcpy(&tx->account_balances[index], account_balance, sizeof(grdw_account_balance));
  return ARNM_SUCCESS;
}

arnm_result grdw_confirmed_transaction_decode(
    grdw_confirmed_transaction *tx, const arnm_memory_block *binary_src, arnm *allocator
) {
  if (!tx || !binary_src || !binary_src->data || !allocator) { return ARNM_ERROR_NULL_POINTER; }
  if (!binary_src->size) { return ARNM_ERROR_INVALID_PARAM; }

  // The workspace stays put on every failing exit below — see pb_decode.h, it is deliberate.
  arnm_memory_block workspace;
  arnm_result result = grdw_pb_workspace_take(&workspace, allocator);
  if (ARNM_SUCCESS != result) { return result; }

  struct proto_gradido_confirmed_transaction_t *proto_tx =
      proto_gradido_confirmed_transaction_new(workspace.data, workspace.size);
  if (!proto_tx) { return ARNM_ERROR_OUT_OF_MEMORY; }

  int decoded =
      proto_gradido_confirmed_transaction_decode(proto_tx, binary_src->data, binary_src->size);
  result = grdw_pb_decode_finish(
      &workspace, proto_tx->base.heap_p->pos, decoded, binary_src->size, allocator
  );
  if (ARNM_SUCCESS != result) { return result; }

  result = grdm_confirmed_transaction_from_pb(tx, proto_tx, allocator);
  arnm_memory_block_free(&workspace, allocator);
  return result;
}

arnm_result grdw_confirmed_transaction_encode(
    arnm_memory_block *binary_dst,
    int *final_size,
    const grdw_confirmed_transaction *tx,
    arnm *allocator
) {
  if (!binary_dst || !tx || !allocator) { return ARNM_ERROR_NULL_POINTER; }
  if (!binary_dst->size) { return ARNM_ERROR_INVALID_PARAM; }

  // TODO: replace with more adaptable strategy
  arnm_memory_block pbBuffer;
  // take whole static area from allocator for pbtools
  arnm_result result =
      arnm_memory_block_alloc(&pbBuffer, arnm_arena_remaining(allocator), allocator);
  if (ARNM_SUCCESS != result) { return result; }

  struct proto_gradido_confirmed_transaction_t *proto_tx;
  proto_tx = proto_gradido_confirmed_transaction_new(pbBuffer.data, pbBuffer.size);
  if (!proto_tx) { return ARNM_ERROR_OUT_OF_MEMORY; }

  result = grdm_confirmed_transaction_from_wire(proto_tx, tx);
  if (ARNM_SUCCESS != result) { return result; }

  int resultSize =
      proto_gradido_confirmed_transaction_encode(proto_tx, binary_dst->data, binary_dst->size);

  arnm_memory_block_free(&pbBuffer, allocator);

  if (PBTOOLS_ENCODE_BUFFER_FULL == -resultSize) { return ARNM_ERROR_DESTINATION_BUFFER_TO_SMALL; }
  if (PBTOOLS_OUT_OF_MEMORY == -resultSize) { return ARNM_ERROR_OUT_OF_MEMORY; }
  if (resultSize < 0) { return ARNM_ERROR_ENCODE_FAILED; }
  if (final_size) { *final_size = resultSize; }
  return ARNM_SUCCESS;
}

void grdw_confirmed_transaction_free(grdw_confirmed_transaction *tx, arnm *allocator) {
  if (!tx) { return; }
  // inner transaction first: it is allocated after account_balances, so an arena unwinds in order
  grdw_gradido_transaction_free(&tx->transaction, allocator);
  arnm_free(
      (uint8_t *)tx->account_balances, sizeof(grdw_account_balance) * tx->account_balances_count,
      allocator
  );
  grdw_confirmed_transaction_init(tx);
}
