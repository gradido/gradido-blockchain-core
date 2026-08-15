#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/data/proto/gradido/gradido_transaction.h"
#include "gradido_blockchain_core/mapping/pbtools_from_wire.h"
#include "gradido_blockchain_core/mapping/wire_from_pbtools.h"
#include "gradido_blockchain_core/result.h"
#include "hostmem/memory.h"

#include <string.h>

#define STATIC_BUFFER_SIZE 2048

void grdw_gradido_transaction_init(grdw_gradido_transaction *tx) {
  memset(tx, 0, sizeof(grdw_gradido_transaction));
}

hostmem_result grdw_gradido_transaction_reserve_sig_map(
    grdw_gradido_transaction *tx, uint8_t sig_map_count, hostmem *allocator
) {
  if (!allocator || !tx) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!sig_map_count) { return HOSTMEM_ERROR_INVALID_PARAM; }
  hostmem_result result = hostmem_alloc(
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
    grdw_gradido_transaction *tx, const hostmem_memory_block *binary_src, hostmem *allocator
) {
  if (!tx || !binary_src || !binary_src->data || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!binary_src->size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  // TODO: calculate needed memory beforhand
  hostmem_memory_block pb_buffer;
  // take whole static area from allocator for pbtools
  hostmem_result result = hostmem_memory_block_alloc(
      &pb_buffer, allocator->capacity - allocator->last_index, allocator
  );
  if (HOSTMEM_SUCCESS != result) { return result; }

  struct proto_gradido_gradido_transaction_t *proto_tx;
  proto_tx = proto_gradido_gradido_transaction_new(pb_buffer.data, pb_buffer.size);
  if (!proto_tx) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  int resultSize =
      proto_gradido_gradido_transaction_decode(proto_tx, binary_src->data, binary_src->size);

  // release not from pbtools used part from allocator
  hostmem_memory_block_realloc(&pb_buffer, (uint32_t)proto_tx->base.heap_p->pos, allocator);

  if (PBTOOLS_OUT_OF_MEMORY == -resultSize) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  if (resultSize < 0 || (uint32_t)resultSize != binary_src->size) {
    return HOSTMEM_ERROR_ENCODE_FAILED;
  }

  result = grdm_gradido_transaction_from_pb(tx, proto_tx, allocator);
  hostmem_memory_block_free(&pb_buffer, allocator);
  return result;
}

hostmem_result grdw_gradido_transaction_encode(
    hostmem_memory_block *binary_dst,
    int *final_size,
    const grdw_gradido_transaction *tx,
    hostmem *allocator
) {
  if (!binary_dst || !tx || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!binary_dst->size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  // TODO: replace with more adaptable strategy
  hostmem_memory_block pb_buffer;
  // take whole static area from allocator for pbtools
  hostmem_result result = hostmem_memory_block_alloc(
      &pb_buffer, allocator->capacity - allocator->last_index, allocator
  );
  if (HOSTMEM_SUCCESS != result) { return result; }

  struct proto_gradido_gradido_transaction_t *proto_tx;
  proto_tx = proto_gradido_gradido_transaction_new(pb_buffer.data, pb_buffer.size);
  if (!proto_tx) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }

  result = grdm_gradido_transaction_from_wire(proto_tx, tx);
  if (HOSTMEM_SUCCESS != result) { return result; }

  int resultSize =
      proto_gradido_gradido_transaction_encode(proto_tx, binary_dst->data, binary_dst->size);

  hostmem_memory_block_free(&pb_buffer, allocator);

  if (PBTOOLS_ENCODE_BUFFER_FULL == -resultSize) {
    return HOSTMEM_ERROR_DESTINATION_BUFFER_TO_SMALL;
  }
  if (PBTOOLS_OUT_OF_MEMORY == -resultSize) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  if (resultSize < 0) { return HOSTMEM_ERROR_ENCODE_FAILED; }
  if (final_size) { *final_size = resultSize; }
  return HOSTMEM_SUCCESS;
}

void grdw_gradido_transaction_free(grdw_gradido_transaction *tx, hostmem *allocator) {
  if (!tx || !allocator) { return; }
  // body_bytes first: it is allocated after sig_map, so an arena unwinds in order
  hostmem_memory_block_free(&tx->body_bytes, allocator);
  hostmem_free((uint8_t *)tx->sig_map, sizeof(grdw_signature_pair) * tx->sig_map_count, allocator);
  grdw_gradido_transaction_init(tx);
}
