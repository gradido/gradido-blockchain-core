#include "pb_decode.h"

#include "gradido_blockchain_core/data/proto/gradido/confirmed_transaction.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"
#include "gradido_blockchain_core/mapping/pbtools_from_wire.h"
#include "gradido_blockchain_core/mapping/wire_from_pbtools.h"
#include "gradido_blockchain_core/result.h"
#include "hostmem/memory.h"

#include <string.h>

void grdw_confirmed_transaction_init(grdw_confirmed_transaction *tx) {
  if (!tx) { return; }
  memset(tx, 0, sizeof(grdw_confirmed_transaction));
  grdw_gradido_transaction_init(&tx->transaction);
}

hostmem_result grdw_confirmed_transaction_reserve_account_balances(
    grdw_confirmed_transaction *tx, uint8_t account_balances_count, hostmem *allocator
) {
  if (!tx || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!account_balances_count) { return HOSTMEM_ERROR_INVALID_PARAM; }
  hostmem_result result = hostmem_alloc(
      (uint8_t **)&tx->account_balances, sizeof(grdw_account_balance) * account_balances_count,
      allocator
  );
  if (HOSTMEM_SUCCESS != result) { return result; }

  tx->account_balances_count = account_balances_count;
  return HOSTMEM_SUCCESS;
}

hostmem_result grdw_confirmed_transaction_copy_account_balance(
    grdw_confirmed_transaction *tx, grdw_account_balance *account_balance, uint8_t index
) {
  if (!tx || !account_balance) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (index >= tx->account_balances_count) { return HOSTMEM_ERROR_INVALID_PARAM; }
  memcpy(&tx->account_balances[index], account_balance, sizeof(grdw_account_balance));
  return HOSTMEM_SUCCESS;
}

hostmem_result grdw_confirmed_transaction_decode(
    grdw_confirmed_transaction *tx, const hostmem_memory_block *binary_src, hostmem *allocator
) {
  if (!tx || !binary_src || !binary_src->data || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!binary_src->size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  // The workspace stays put on every failing exit below — see pb_decode.h, it is deliberate.
  hostmem_memory_block workspace;
  hostmem_result result = grdw_pb_workspace_take(&workspace, allocator);
  if (HOSTMEM_SUCCESS != result) { return result; }

  struct proto_gradido_confirmed_transaction_t *proto_tx =
      proto_gradido_confirmed_transaction_new(workspace.data, workspace.size);
  if (!proto_tx) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }

  int decoded =
      proto_gradido_confirmed_transaction_decode(proto_tx, binary_src->data, binary_src->size);
  result = grdw_pb_decode_finish(
      &workspace, proto_tx->base.heap_p->pos, decoded, binary_src->size, allocator
  );
  if (HOSTMEM_SUCCESS != result) { return result; }

  result = grdm_confirmed_transaction_from_pb(tx, proto_tx, allocator);
  hostmem_memory_block_free(&workspace, allocator);
  return result;
}

hostmem_result grdw_confirmed_transaction_encode(
    hostmem_memory_block *binary_dst,
    int *final_size,
    const grdw_confirmed_transaction *tx,
    hostmem *allocator
) {
  if (!binary_dst || !tx || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!binary_dst->size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  // TODO: replace with more adaptable strategy
  hostmem_memory_block pbBuffer;
  // take whole static area from allocator for pbtools
  hostmem_result result =
      hostmem_memory_block_alloc(&pbBuffer, allocator->capacity - allocator->last_index, allocator);
  if (HOSTMEM_SUCCESS != result) { return result; }

  struct proto_gradido_confirmed_transaction_t *proto_tx;
  proto_tx = proto_gradido_confirmed_transaction_new(pbBuffer.data, pbBuffer.size);
  if (!proto_tx) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }

  result = grdm_confirmed_transaction_from_wire(proto_tx, tx);
  if (HOSTMEM_SUCCESS != result) { return result; }

  int resultSize =
      proto_gradido_confirmed_transaction_encode(proto_tx, binary_dst->data, binary_dst->size);

  hostmem_memory_block_free(&pbBuffer, allocator);

  if (PBTOOLS_ENCODE_BUFFER_FULL == -resultSize) {
    return HOSTMEM_ERROR_DESTINATION_BUFFER_TO_SMALL;
  }
  if (PBTOOLS_OUT_OF_MEMORY == -resultSize) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  if (resultSize < 0) { return HOSTMEM_ERROR_ENCODE_FAILED; }
  if (final_size) { *final_size = resultSize; }
  return HOSTMEM_SUCCESS;
}

void grdw_confirmed_transaction_free(grdw_confirmed_transaction *tx, hostmem *allocator) {
  if (!tx) { return; }
  // inner transaction first: it is allocated after account_balances, so an arena unwinds in order
  grdw_gradido_transaction_free(&tx->transaction, allocator);
  hostmem_free(
      (uint8_t *)tx->account_balances, sizeof(grdw_account_balance) * tx->account_balances_count,
      allocator
  );
  grdw_confirmed_transaction_init(tx);
}
