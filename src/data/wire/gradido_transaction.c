#include "gradido_blockchain_core/data/wire/pb_workspace.h"

#include "gradido_blockchain_core/data/proto/gradido/gradido_transaction.h"
#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/mapping/pbtools_from_wire.h"
#include "gradido_blockchain_core/mapping/wire_from_pbtools.h"
#include "gradido_blockchain_core/result.h"
#include "hostmem/memory.h"

#include <string.h>

#define STATIC_BUFFER_SIZE 2048

void grdw_gradido_transaction_init(grdw_gradido_transaction *tx) {
  if (!tx) { return; }
  memset(tx, 0, sizeof(grdw_gradido_transaction));
}

hostmem_result grdw_gradido_transaction_reserve_sig_map(
    grdw_gradido_transaction *tx, uint8_t sig_map_count, hostmem_multi_arena *allocator
) {
  if (!allocator || !tx) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!sig_map_count) { return HOSTMEM_ERROR_INVALID_PARAM; }
  hostmem_result result = hostmem_multi_arena_alloc(
      (uint8_t **)&tx->sig_map, sizeof(grdw_signature_pair) * sig_map_count, allocator
  );
  if (HOSTMEM_SUCCESS != result) { return result; }

  tx->sig_map_count = sig_map_count;
  return HOSTMEM_SUCCESS;
}

hostmem_result grdw_gradido_transaction_copy_sig_map(
    grdw_gradido_transaction *tx, const grdw_signature_pair *sig_map, uint8_t index
) {
  if (!tx || !sig_map) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (index >= tx->sig_map_count) { return HOSTMEM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS; }
  memcpy(&tx->sig_map[index], sig_map, sizeof(grdw_signature_pair));
  return HOSTMEM_SUCCESS;
}

/*
*
#define PBTOOLS_BAD_WIRE_TYPE                                   1
#define PBTOOLS_OUT_OF_DATA                                     2
#define PBTOOLS_OUT_OF_MEMORY                                   3
#define PBTOOLS_ENCODE_BUFFER_FULL                              4
#define PBTOOLS_BAD_FIELD_NUMBER                                5
#define PBTOOLS_VARINT_OVERFLOW                                 6
#define PBTOOLS_SEEK_OVERFLOW                                   7
#define PBTOOLS_LENGTH_DELIMITED_OVERFLOW                       8
*/

hostmem_result grdw_gradido_transaction_decode(
    grdw_gradido_transaction *tx,
    const hostmem_memory_block *binary_src,
    const hostmem_memory_block *workspace,
    hostmem_multi_arena *allocator
) {
  if (!tx || !binary_src || !binary_src->data || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!binary_src->size) { return HOSTMEM_ERROR_INVALID_PARAM; }
  hostmem_result usable = grdw_pb_workspace_check(workspace);
  if (HOSTMEM_SUCCESS != usable) { return usable; }

  struct proto_gradido_gradido_transaction_t *proto =
      proto_gradido_gradido_transaction_new(workspace->data, workspace->size);
  // too small even for the empty message, which is the same answer as running out during the
  // decode: the caller enlarges the stretch and calls again
  if (!proto) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }

  int decoded = proto_gradido_gradido_transaction_decode(proto, binary_src->data, binary_src->size);
  hostmem_result verdict = grdw_pb_decode_verdict(decoded, binary_src->size);
  if (HOSTMEM_SUCCESS != verdict) { return verdict; }

  // what the wire structure keeps is copied out of the workspace here, which is what leaves the
  // caller free to hand the same stretch to the next message
  return grdm_gradido_transaction_from_pb(tx, proto, allocator);
}

hostmem_result grdw_gradido_transaction_encode(
    hostmem_memory_block *binary_dst,
    int *final_size,
    const grdw_gradido_transaction *tx,
    const hostmem_memory_block *workspace
) {
  if (!binary_dst || !binary_dst->data || !tx) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!binary_dst->size) { return HOSTMEM_ERROR_INVALID_PARAM; }
  hostmem_result usable = grdw_pb_workspace_check(workspace);
  if (HOSTMEM_SUCCESS != usable) { return usable; }

  struct proto_gradido_gradido_transaction_t *proto =
      proto_gradido_gradido_transaction_new(workspace->data, workspace->size);
  if (!proto) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }

  // building the message allocates its repeated fields in the workspace, so this is the first
  // place a stretch that is too small says so
  hostmem_result result = grdm_gradido_transaction_from_wire(proto, tx);
  if (HOSTMEM_SUCCESS != result) { return result; }

  return grdw_pb_encode_verdict(
      final_size,
      proto_gradido_gradido_transaction_encode(proto, binary_dst->data, binary_dst->size)
  );
}

void grdw_gradido_transaction_free(grdw_gradido_transaction *tx, hostmem_multi_arena *allocator) {
  if (!tx || !allocator) { return; }
  // body_bytes first: it is allocated after sig_map, so an arena unwinds in order
  grdw_block_free(&tx->body_bytes, allocator);
  hostmem_multi_arena_free(
      (uint8_t *)tx->sig_map, sizeof(grdw_signature_pair) * tx->sig_map_count, allocator
  );
  grdw_gradido_transaction_init(tx);
}
