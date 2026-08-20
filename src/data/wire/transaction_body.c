#include "gradido_blockchain_core/data/wire/pb_workspace.h"

#include "gradido_blockchain_core/data/proto/gradido/transaction_body.h"
#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "gradido_blockchain_core/mapping/pbtools_from_wire.h"
#include "gradido_blockchain_core/mapping/wire_from_pbtools.h"
#include "gradido_blockchain_core/result.h"
#include "hostmem/memory.h"

#include <string.h>

void grdw_transaction_body_init(grdw_transaction_body *body) {
  if (!body) { return; }
  memset(body, 0, sizeof(grdw_transaction_body));
}

hostmem_result grdw_transaction_body_reserve_memos(
    grdw_transaction_body *body, size_t memos_count, hostmem_multi_arena *allocator
) {
  if (!body) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (memos_count > 255) { return HOSTMEM_ERROR_INVALID_PARAM; }
  hostmem_result result = hostmem_multi_arena_alloc(
      (uint8_t **)&body->memos, sizeof(grdw_encrypted_memo) * memos_count, allocator
  );
  if (HOSTMEM_SUCCESS != result) { return result; }

  body->memos_count = memos_count;
  return HOSTMEM_SUCCESS;
}

hostmem_result grdw_transaction_body_move_memo(
    grdw_transaction_body *body, grdw_encrypted_memo *memo, uint8_t index
) {
  if (!body || !body->memos || !memo) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (index >= body->memos_count) { return HOSTMEM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS; }
  memcpy(&body->memos[index], memo, sizeof(grdw_encrypted_memo));
  // set memo data ptr to zero
  memo->memo.data = NULL;
  memo->memo.size = 0;
  return HOSTMEM_SUCCESS;
}

hostmem_result grdw_transaction_body_copy_memo(
    grdw_transaction_body *body,
    const grdw_encrypted_memo *memo,
    uint8_t index,
    hostmem_multi_arena *allocator
) {
  if (!body || !body->memos || !memo || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (index >= body->memos_count) { return HOSTMEM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS; }
  body->memos[index].type = memo->type;
  return grdw_block_clone(&body->memos[index].memo, &memo->memo, allocator);
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
hostmem_result grdw_transaction_body_decode(
    grdw_transaction_body *body,
    const hostmem_memory_block *binarySrc,
    const hostmem_memory_block *workspace,
    hostmem_multi_arena *allocator
) {
  if (!body || !binarySrc || !binarySrc->data || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!binarySrc->size) { return HOSTMEM_ERROR_INVALID_PARAM; }
  hostmem_result usable = grdw_pb_workspace_check(workspace);
  if (HOSTMEM_SUCCESS != usable) { return usable; }

  struct proto_gradido_transaction_body_t *proto =
      proto_gradido_transaction_body_new(workspace->data, workspace->size);
  // too small even for the empty message, which is the same answer as running out during the
  // decode: the caller enlarges the stretch and calls again
  if (!proto) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }

  int decoded = proto_gradido_transaction_body_decode(proto, binarySrc->data, binarySrc->size);
  hostmem_result verdict = grdw_pb_decode_verdict(decoded, binarySrc->size);
  if (HOSTMEM_SUCCESS != verdict) { return verdict; }

  // what the wire structure keeps is copied out of the workspace here, which is what leaves the
  // caller free to hand the same stretch to the next message
  return grdm_transaction_body_from_pbtools(body, proto, allocator);
}

hostmem_result grdw_transaction_body_encode(
    hostmem_memory_block *binaryDst,
    int *final_size,
    const grdw_transaction_body *body,
    const hostmem_memory_block *workspace
) {
  if (!binaryDst || !binaryDst->data || !body) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!binaryDst->size) { return HOSTMEM_ERROR_INVALID_PARAM; }
  hostmem_result usable = grdw_pb_workspace_check(workspace);
  if (HOSTMEM_SUCCESS != usable) { return usable; }

  struct proto_gradido_transaction_body_t *proto =
      proto_gradido_transaction_body_new(workspace->data, workspace->size);
  if (!proto) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }

  // building the message allocates its repeated fields in the workspace, so this is the first
  // place a stretch that is too small says so
  hostmem_result result = grdm_transaction_body_from_wire(proto, body);
  if (HOSTMEM_SUCCESS != result) { return result; }

  return grdw_pb_encode_verdict(
      final_size, proto_gradido_transaction_body_encode(proto, binaryDst->data, binaryDst->size)
  );
}

void grdw_transaction_body_free(grdw_transaction_body *body, hostmem_multi_arena *allocator) {
  if (!body || !allocator) { return; }
  if (body->memos_count) {
    // backwards, so an arena unwinds in allocation order and actually gets the bytes back
    for (int i = body->memos_count - 1; i >= 0; i--) {
      grdw_block_free(&body->memos[i].memo, allocator);
    }
    hostmem_multi_arena_free(
        (uint8_t *)body->memos, sizeof(grdw_encrypted_memo) * body->memos_count, allocator
    );
  }
  // reserve/from_pbtools always take exactly HOSTMEM_UUID_BINARY_SIZE for this one
  hostmem_multi_arena_free(body->other_community_uuid, HOSTMEM_UUID_BINARY_SIZE, allocator);
  grdw_transaction_body_init(body);
}
