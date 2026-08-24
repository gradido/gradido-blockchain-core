#include "pb_decode.h"

#include "arnm/arena.h"
#include "gradido_blockchain_core/data/proto/gradido/gradido_transaction.h"
#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/mapping/pbtools_from_wire.h"
#include "gradido_blockchain_core/mapping/wire_from_pbtools.h"
#include "gradido_blockchain_core/result.h"

#include <string.h>

#define STATIC_BUFFER_SIZE 2048

void grdw_gradido_transaction_init(grdw_gradido_transaction *tx) {
  if (!tx) { return; }
  memset(tx, 0, sizeof(grdw_gradido_transaction));
}

arnm_result grdw_gradido_transaction_reserve_sig_map(
    grdw_gradido_transaction *tx, uint8_t sig_map_count, arnm *allocator
) {
  if (!allocator || !tx) { return ARNM_ERROR_NULL_POINTER; }
  if (!sig_map_count) { return ARNM_ERROR_INVALID_PARAM; }
  arnm_result result =
      arnm_alloc((uint8_t **)&tx->sig_map, sizeof(grdw_signature_pair) * sig_map_count, allocator);
  if (ARNM_SUCCESS != result) { return result; }

  tx->sig_map_count = sig_map_count;
  return ARNM_SUCCESS;
}

arnm_result grdw_gradido_transaction_copy_sig_map(
    grdw_gradido_transaction *tx, const grdw_signature_pair *sig_map, uint8_t index
) {
  if (!tx || !sig_map) { return ARNM_ERROR_NULL_POINTER; }
  if (index >= tx->sig_map_count) { return ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS; }
  memcpy(&tx->sig_map[index], sig_map, sizeof(grdw_signature_pair));
  return ARNM_SUCCESS;
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

arnm_result grdw_gradido_transaction_decode(
    grdw_gradido_transaction *tx, const arnm_memory_block *binary_src, arnm *allocator
) {
  if (!tx || !binary_src || !binary_src->data || !allocator) { return ARNM_ERROR_NULL_POINTER; }
  if (!binary_src->size) { return ARNM_ERROR_INVALID_PARAM; }

  // The workspace stays put on every failing exit below — see pb_decode.h, it is deliberate.
  arnm_memory_block workspace;
  arnm_result result = grdw_pb_workspace_take(&workspace, allocator);
  if (ARNM_SUCCESS != result) { return result; }

  struct proto_gradido_gradido_transaction_t *proto_tx =
      proto_gradido_gradido_transaction_new(workspace.data, workspace.size);
  if (!proto_tx) { return ARNM_ERROR_OUT_OF_MEMORY; }

  int decoded =
      proto_gradido_gradido_transaction_decode(proto_tx, binary_src->data, binary_src->size);
  result = grdw_pb_decode_finish(
      &workspace, proto_tx->base.heap_p->pos, decoded, binary_src->size, allocator
  );
  if (ARNM_SUCCESS != result) { return result; }

  result = grdm_gradido_transaction_from_pb(tx, proto_tx, allocator);
  arnm_memory_block_free(&workspace, allocator);
  return result;
}

arnm_result grdw_gradido_transaction_encode(
    arnm_memory_block *binary_dst,
    int *final_size,
    const grdw_gradido_transaction *tx,
    arnm *allocator
) {
  if (!binary_dst || !tx || !allocator) { return ARNM_ERROR_NULL_POINTER; }
  if (!binary_dst->size) { return ARNM_ERROR_INVALID_PARAM; }

  // TODO: replace with more adaptable strategy
  arnm_memory_block pb_buffer;
  // take whole static area from allocator for pbtools
  arnm_result result =
      arnm_memory_block_alloc(&pb_buffer, arnm_arena_remaining(allocator), allocator);
  if (ARNM_SUCCESS != result) { return result; }

  struct proto_gradido_gradido_transaction_t *proto_tx;
  proto_tx = proto_gradido_gradido_transaction_new(pb_buffer.data, pb_buffer.size);
  if (!proto_tx) { return ARNM_ERROR_OUT_OF_MEMORY; }

  result = grdm_gradido_transaction_from_wire(proto_tx, tx);
  if (ARNM_SUCCESS != result) { return result; }

  int resultSize =
      proto_gradido_gradido_transaction_encode(proto_tx, binary_dst->data, binary_dst->size);

  arnm_memory_block_free(&pb_buffer, allocator);

  if (PBTOOLS_ENCODE_BUFFER_FULL == -resultSize) { return ARNM_ERROR_DESTINATION_BUFFER_TO_SMALL; }
  if (PBTOOLS_OUT_OF_MEMORY == -resultSize) { return ARNM_ERROR_OUT_OF_MEMORY; }
  if (resultSize < 0) { return ARNM_ERROR_ENCODE_FAILED; }
  if (final_size) { *final_size = resultSize; }
  return ARNM_SUCCESS;
}

void grdw_gradido_transaction_free(grdw_gradido_transaction *tx, arnm *allocator) {
  if (!tx || !allocator) { return; }
  // body_bytes first: it is allocated after sig_map, so an arena unwinds in order
  arnm_memory_block_free(&tx->body_bytes, allocator);
  arnm_free((uint8_t *)tx->sig_map, sizeof(grdw_signature_pair) * tx->sig_map_count, allocator);
  grdw_gradido_transaction_init(tx);
}
