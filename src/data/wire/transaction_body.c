#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "gradido_blockchain_core/mapping/pbtools_from_wire.h"
#include "gradido_blockchain_core/mapping/wire_from_pbtools.h"
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"
#include "transaction_body.h"

#include <string.h>

#define STATIC_BUFFER_SIZE 2048

void grdw_transaction_body_init(grdw_transaction_body *body) {
  body->memos = NULL;
  body->other_community_uuid = NULL;
  body->created_at = (grdw_timestamp){.seconds = 0, .nanos = 0};
  body->transaction_type = GRDD_TRANSACTION_TYPE_NONE;
  body->type = GRDD_CROSS_GROUP_TYPE_LOCAL;
  body->memos_count = 0;
}

grd_result grdw_transaction_body_reserve_memos(
    grdw_transaction_body *body, size_t memos_count, grd_memory *allocator
) {
  grd_result result = grd_memory_buffer_alloc(
      (uint8_t **)&body->memos, allocator, sizeof(grdw_encrypted_memo) * memos_count
  );
  if (GRD_SUCCESS != result) { return result; }

  body->memos_count = memos_count;
  return GRD_SUCCESS;
}

grd_result grdw_transaction_body_move_memo(
    grdw_transaction_body *body, grdw_encrypted_memo *memo, uint8_t index
) {
  if (!body || !body->memos || !memo) { return GRD_ERROR_NULL_POINTER; }
  if (index >= body->memos_count) { return GRD_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS; }
  memcpy(&body->memos[index], memo, sizeof(grdw_encrypted_memo));
  // set memo data ptr to zero
  memo->memo.data = NULL;
  memo->memo.size = 0;
  return GRD_SUCCESS;
}

grd_result grdw_transaction_body_copy_memo(
    grdw_transaction_body *body,
    const grdw_encrypted_memo *memo,
    uint8_t index,
    grd_memory *allocator
) {
  if (!body || !body->memos || !memo || !allocator) { return GRD_ERROR_NULL_POINTER; }
  if (index >= body->memos_count) { return GRD_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS; }
  body->memos[index].type = memo->type;
  return grd_memory_block_copy(&body->memos[index].memo, &memo->memo, allocator);
}

grd_result grdw_transaction_body_decode(
    grdw_transaction_body *body, const grd_memory_block *binarySrc, grd_memory *allocator
) {
  if (!body || !binarySrc || !binarySrc->data || !allocator) { return GRD_ERROR_NULL_POINTER; }
  if (!binarySrc->size) { return GRD_ERROR_INVALID_PARAM; }

  uint8_t buffer[STATIC_BUFFER_SIZE];
  // TODO: test with different composed tx if it is always enough
  size_t bufferSize = STATIC_BUFFER_SIZE;

  struct proto_gradido_transaction_body_t *proto_body;
  proto_body = proto_gradido_transaction_body_new(buffer, bufferSize);
  if (!proto_body) { return GRD_ERROR_STATIC_BUFFER_TO_SMALL; }
  int resultSize =
      proto_gradido_transaction_body_decode(proto_body, binarySrc->data, binarySrc->size);

  if (resultSize < 0) { return GRD_ERROR_STATIC_BUFFER_TO_SMALL; }
  if (resultSize != binarySrc->size) { return GRD_ERROR_ENCODE_FAILED; }

  return grdm_transaction_body_from_pbtools(body, proto_body, allocator);
}

grd_result grdw_transaction_body_encode(
    grd_memory_block *binaryDst,
    size_t *final_size,
    const grdw_transaction_body *body,
    grd_memory *allocator
) {
  if (!binaryDst || !body) { return GRD_ERROR_NULL_POINTER; }
  if (!binaryDst->size) { return GRD_ERROR_INVALID_PARAM; }
  // TODO: replace with more adaptable strategy
  uint8_t buffer[STATIC_BUFFER_SIZE];
  struct proto_gradido_transaction_body_t *proto_body;
  proto_body = proto_gradido_transaction_body_new(buffer, STATIC_BUFFER_SIZE);
  if (!proto_body) { return GRD_ERROR_STATIC_BUFFER_TO_SMALL; }

  grd_result result = grdm_transaction_body_from_wire(proto_body, body, allocator);
  if (GRD_SUCCESS != result) { return result; }

  int resultSize =
      proto_gradido_transaction_body_encode(proto_body, binaryDst->data, binaryDst->size);
  if (resultSize <= 0) { return GRD_ERROR_ENCODE_FAILED; }
  if (final_size) { *final_size = resultSize; }
  return GRD_SUCCESS;
}

void grdw_transaction_body_free(grdw_transaction_body *body, grd_memory *allocator) {
  if (!body || !allocator) { return; }
  if (body->memos_count) {
    for (int i = 0; i < body->memos_count; i++) {
      grd_memory_block_free(&body->memos[i].memo, allocator);
    }
    grd_memory_buffer_free((uint8_t *)body->memos, allocator);
  }
  if (body->other_community_uuid) { grd_memory_buffer_free(body->other_community_uuid, allocator); }
  grdw_transaction_body_init(body);
}
