#include "gradido_blockchain_core/data/wire/pb_workspace.h"

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
    grdw_confirmed_transaction *tx, uint8_t account_balances_count, hostmem_multi_arena *allocator
) {
  if (!tx || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!account_balances_count) { return HOSTMEM_ERROR_INVALID_PARAM; }
  hostmem_result result = hostmem_multi_arena_alloc(
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
    grdw_confirmed_transaction *tx,
    const hostmem_memory_block *binary_src,
    const hostmem_memory_block *workspace,
    hostmem_multi_arena *allocator
) {
  if (!tx || !binary_src || !binary_src->data || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!binary_src->size) { return HOSTMEM_ERROR_INVALID_PARAM; }
  hostmem_result usable = grdw_pb_workspace_check(workspace);
  if (HOSTMEM_SUCCESS != usable) { return usable; }

  struct proto_gradido_confirmed_transaction_t *proto =
      proto_gradido_confirmed_transaction_new(workspace->data, workspace->size);
  // too small even for the empty message, which is the same answer as running out during the
  // decode: the caller enlarges the stretch and calls again
  if (!proto) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }

  int decoded =
      proto_gradido_confirmed_transaction_decode(proto, binary_src->data, binary_src->size);
  hostmem_result verdict = grdw_pb_decode_verdict(decoded, binary_src->size);
  if (HOSTMEM_SUCCESS != verdict) { return verdict; }

  // what the wire structure keeps is copied out of the workspace here, which is what leaves the
  // caller free to hand the same stretch to the next message
  return grdm_confirmed_transaction_from_pb(tx, proto, allocator);
}

hostmem_result grdw_confirmed_transaction_encode(
    hostmem_memory_block *binary_dst,
    int *final_size,
    const grdw_confirmed_transaction *tx,
    const hostmem_memory_block *workspace
) {
  if (!binary_dst || !binary_dst->data || !tx) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!binary_dst->size) { return HOSTMEM_ERROR_INVALID_PARAM; }
  hostmem_result usable = grdw_pb_workspace_check(workspace);
  if (HOSTMEM_SUCCESS != usable) { return usable; }

  struct proto_gradido_confirmed_transaction_t *proto =
      proto_gradido_confirmed_transaction_new(workspace->data, workspace->size);
  if (!proto) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }

  // building the message allocates its repeated fields in the workspace, so this is the first
  // place a stretch that is too small says so
  hostmem_result result = grdm_confirmed_transaction_from_wire(proto, tx);
  if (HOSTMEM_SUCCESS != result) { return result; }

  return grdw_pb_encode_verdict(
      final_size,
      proto_gradido_confirmed_transaction_encode(proto, binary_dst->data, binary_dst->size)
  );
}

void grdw_confirmed_transaction_free(
    grdw_confirmed_transaction *tx, hostmem_multi_arena *allocator
) {
  if (!tx) { return; }
  // inner transaction first: it is allocated after account_balances, so an arena unwinds in order
  grdw_gradido_transaction_free(&tx->transaction, allocator);
  hostmem_multi_arena_free(
      (uint8_t *)tx->account_balances, sizeof(grdw_account_balance) * tx->account_balances_count,
      allocator
  );
  grdw_confirmed_transaction_init(tx);
}
