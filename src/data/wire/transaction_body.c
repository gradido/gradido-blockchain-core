#include "pb_decode.h"

#include "arnm/arena.h"
#include "gradido_blockchain_core/data/proto/gradido/transaction_body.h"
#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "gradido_blockchain_core/mapping/pbtools_from_wire.h"
#include "gradido_blockchain_core/mapping/wire_from_pbtools.h"
#include "gradido_blockchain_core/result.h"

#include <string.h>

void grdw_transaction_body_init(grdw_transaction_body *body) {
  if (!body) { return; }
  memset(body, 0, sizeof(grdw_transaction_body));
}

arnm_result grdw_transaction_body_reserve_memos(
    grdw_transaction_body *body, size_t memos_count, arnm *allocator
) {
  if (!body) { return ARNM_ERROR_NULL_POINTER; }
  if (memos_count > 255) { return ARNM_ERROR_INVALID_PARAM; }
  arnm_result result =
      arnm_alloc((uint8_t **)&body->memos, sizeof(grdw_encrypted_memo) * memos_count, allocator);
  if (ARNM_SUCCESS != result) { return result; }

  body->memos_count = memos_count;
  return ARNM_SUCCESS;
}

arnm_result grdw_transaction_body_move_memo(
    grdw_transaction_body *body, grdw_encrypted_memo *memo, uint8_t index
) {
  if (!body || !body->memos || !memo) { return ARNM_ERROR_NULL_POINTER; }
  if (index >= body->memos_count) { return ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS; }
  memcpy(&body->memos[index], memo, sizeof(grdw_encrypted_memo));
  // set memo data ptr to zero
  memo->memo.data = NULL;
  memo->memo.size = 0;
  return ARNM_SUCCESS;
}

arnm_result grdw_transaction_body_copy_memo(
    grdw_transaction_body *body, const grdw_encrypted_memo *memo, uint8_t index, arnm *allocator
) {
  if (!body || !body->memos || !memo || !allocator) { return ARNM_ERROR_NULL_POINTER; }
  if (index >= body->memos_count) { return ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS; }
  body->memos[index].type = memo->type;
  return arnm_memory_block_clone(&body->memos[index].memo, &memo->memo, allocator);
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
arnm_result grdw_transaction_body_decode(
    grdw_transaction_body *body, const arnm_memory_block *binarySrc, arnm *allocator
) {
  if (!body || !binarySrc || !binarySrc->data || !allocator) { return ARNM_ERROR_NULL_POINTER; }
  if (!binarySrc->size) { return ARNM_ERROR_INVALID_PARAM; }

  // The workspace stays put on every failing exit below — see pb_decode.h, it is deliberate.
  arnm_memory_block workspace;
  arnm_result result = grdw_pb_workspace_take(&workspace, allocator);
  if (ARNM_SUCCESS != result) { return result; }

  struct proto_gradido_transaction_body_t *proto_body =
      proto_gradido_transaction_body_new(workspace.data, workspace.size);
  if (!proto_body) { return ARNM_ERROR_OUT_OF_MEMORY; }

  int decoded = proto_gradido_transaction_body_decode(proto_body, binarySrc->data, binarySrc->size);
  result = grdw_pb_decode_finish(
      &workspace, proto_body->base.heap_p->pos, decoded, binarySrc->size, allocator
  );
  if (ARNM_SUCCESS != result) { return result; }

  result = grdm_transaction_body_from_pbtools(body, proto_body, allocator);
  arnm_memory_block_free(&workspace, allocator);
  return result;
}

arnm_result grdw_transaction_body_encode(
    arnm_memory_block *binaryDst,
    int *final_size,
    const grdw_transaction_body *body,
    arnm *allocator
) {
  if (!binaryDst || !body || !allocator) { return ARNM_ERROR_NULL_POINTER; }
  if (!binaryDst->size) { return ARNM_ERROR_INVALID_PARAM; }

  // TODO: replace with more adaptable strategy
  arnm_memory_block pbBuffer;
  // take whole static area from allocator for pbtools
  arnm_result result =
      arnm_memory_block_alloc(&pbBuffer, arnm_arena_remaining(allocator), allocator);
  if (ARNM_SUCCESS != result) { return result; }

  struct proto_gradido_transaction_body_t *proto_body;
  proto_body = proto_gradido_transaction_body_new(pbBuffer.data, pbBuffer.size);
  if (!proto_body) { return ARNM_ERROR_OUT_OF_MEMORY; }

  result = grdm_transaction_body_from_wire(proto_body, body);
  if (ARNM_SUCCESS != result) { return result; }

  int resultSize =
      proto_gradido_transaction_body_encode(proto_body, binaryDst->data, binaryDst->size);

  arnm_memory_block_free(&pbBuffer, allocator);

  if (PBTOOLS_ENCODE_BUFFER_FULL == -resultSize) { return ARNM_ERROR_DESTINATION_BUFFER_TO_SMALL; }
  if (PBTOOLS_OUT_OF_MEMORY == -resultSize) { return ARNM_ERROR_OUT_OF_MEMORY; }
  if (resultSize < 0) { return ARNM_ERROR_ENCODE_FAILED; }
  if (final_size) { *final_size = resultSize; }
  return ARNM_SUCCESS;
}

void grdw_transaction_body_free(grdw_transaction_body *body, arnm *allocator) {
  if (!body || !allocator) { return; }
  if (body->memos_count) {
    // backwards, so an arena unwinds in allocation order and actually gets the bytes back
    for (int i = body->memos_count - 1; i >= 0; i--) {
      arnm_memory_block_free(&body->memos[i].memo, allocator);
    }
    arnm_free((uint8_t *)body->memos, sizeof(grdw_encrypted_memo) * body->memos_count, allocator);
  }
  // reserve/from_pbtools always take exactly ARNM_UUID_BINARY_SIZE for this one
  arnm_free(body->other_community_uuid, ARNM_UUID_BINARY_SIZE, allocator);
  grdw_transaction_body_init(body);
}
