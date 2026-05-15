#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/mapping/pbtools_from_wire.h"
#include "gradido_blockchain_core/mapping/wire_from_pbtools.h"
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_transaction.h"

#include <string.h>

#define STATIC_BUFFER_SIZE 2048

void grdw_gradido_transaction_init(grdw_gradido_transaction *tx) {
  tx->sig_map = NULL;
  tx->body_bytes.size = 0;
  tx->body_bytes.data = NULL;
  tx->pairing_ledger_anchor.type = GRDW_LEDGER_ANCHOR_TYPE_UNSPECIFIED;
  tx->sig_map_count = 0;
}

grd_result grdw_gradido_transaction_reserve_sig_map(
    grdw_gradido_transaction *tx, uint8_t sig_map_count, grd_memory *allocator
) {
  if (!allocator || !tx) { return GRD_ERROR_NULL_POINTER; }
  if (!sig_map_count) { return GRD_ERROR_INVALID_PARAM; }
  grd_result result = grd_memory_buffer_alloc(
      (uint8_t **)&tx->sig_map, allocator, sizeof(grdw_signature_pair) * sig_map_count
  );
  if (GRD_SUCCESS != result) { return result; }

  tx->sig_map_count = sig_map_count;
  return GRD_SUCCESS;
}

grd_result grdw_gradido_transaction_copy_sig_map(
    grdw_gradido_transaction *tx, const grdw_signature_pair *sig_map, uint8_t index
) {
  if (!tx || !sig_map) { return GRD_ERROR_NULL_POINTER; }
  if (index >= tx->sig_map_count) { return GRD_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS; }
  memcpy(&tx->sig_map[index], sig_map, sizeof(grdw_signature_pair));
  return GRD_SUCCESS;
}

grd_result grdw_gradido_transaction_decode(
    grdw_gradido_transaction *tx, const grd_memory_block *binarySrc, grd_memory *allocator
) {
  if (!tx || !binarySrc || !binarySrc->data || !allocator) { return GRD_ERROR_NULL_POINTER; }
  if (!binarySrc->size) { return GRD_ERROR_INVALID_PARAM; }

  uint8_t buffer[STATIC_BUFFER_SIZE];
  struct proto_gradido_gradido_transaction_t *proto_tx;
  proto_tx = proto_gradido_gradido_transaction_new(buffer, STATIC_BUFFER_SIZE);
  if (!proto_tx) { return GRD_ERROR_STATIC_BUFFER_TO_SMALL; }
  int resultSize =
      proto_gradido_gradido_transaction_decode(proto_tx, binarySrc->data, binarySrc->size);
  if (resultSize < 0) { return GRD_ERROR_STATIC_BUFFER_TO_SMALL; }
  if (resultSize != binarySrc->size) { return GRD_ERROR_ENCODE_FAILED; }

  return grdm_gradido_transaction_from_pb(tx, proto_tx, allocator);
}

grd_result grdw_gradido_transaction_encode(
    grd_memory_block *binaryDst,
    size_t *final_size,
    const grdw_gradido_transaction *tx,
    grd_memory *allocator
) {
  if (!binaryDst || !tx) { return GRD_ERROR_NULL_POINTER; }
  if (!binaryDst->size) { return GRD_ERROR_INVALID_PARAM; }
  // TODO: replace with more adaptable strategy
  uint8_t buffer[STATIC_BUFFER_SIZE];
  struct proto_gradido_gradido_transaction_t *proto_tx;
  proto_tx = proto_gradido_gradido_transaction_new(buffer, STATIC_BUFFER_SIZE);
  if (!proto_tx) { return GRD_ERROR_STATIC_BUFFER_TO_SMALL; }

  grd_result result = grdm_gradido_transaction_from_wire(proto_tx, tx, allocator);
  if (GRD_SUCCESS != result) { return result; }

  int resultSize =
      proto_gradido_gradido_transaction_encode(proto_tx, binaryDst->data, binaryDst->size);
  if (resultSize <= 0) { return GRD_ERROR_ENCODE_FAILED; }
  if (final_size) { *final_size = resultSize; }
  return GRD_SUCCESS;
}

void grdw_gradido_transaction_free(grdw_gradido_transaction *tx, grd_memory *allocator) {
  if (!tx || !allocator) { return; }
  if (tx->sig_map_count) { grd_memory_buffer_free((uint8_t *)tx->sig_map, allocator); }
  grd_memory_block_free(&tx->body_bytes, allocator);
  grdw_gradido_transaction_init(tx);
}
