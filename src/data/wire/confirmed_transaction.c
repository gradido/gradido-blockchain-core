#include "pb_decode.h"

#include "arnm/arena.h"
#include "gradido_blockchain_core/data/proto/gradido/confirmed_transaction.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"
#include "gradido_blockchain_core/mapping/pbtools_from_wire.h"
#include "gradido_blockchain_core/mapping/wire_from_pbtools.h"
#include "gradido_blockchain_core/result.h"

#include <stdbool.h>
#include <stdint.h>
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

// ********** the number alone *******************

/** Field 1 of ConfirmedTransaction, the transaction number. */
#define PEEK_ID_FIELD 1u
/** Protobuf wire types a top level field of this message can have. */
#define PEEK_WIRE_VARINT 0u
#define PEEK_WIRE_FIXED64 1u
#define PEEK_WIRE_LENGTH 2u
#define PEEK_WIRE_FIXED32 5u

/**
 * One varint at @p *pos, which moves past it. False when it runs off the end or needs more than
 * 64 bits: ten bytes at most, and the tenth may carry only the highest bit.
 */
static bool peek_varint(const uint8_t *data, uint32_t size, uint32_t *pos, uint64_t *out) {
  uint64_t value = 0;
  for (uint32_t shift = 0; shift < 64u; shift += 7u) {
    if (*pos >= size) { return false; }
    const uint8_t byte = data[(*pos)++];
    if (63u == shift && byte > 1u) { return false; }
    value |= (uint64_t)(byte & 0x7Fu) << shift;
    if (!(byte & 0x80u)) {
      *out = value;
      return true;
    }
  }
  return false;
}

/** Steps @p *pos over @p count bytes, false when fewer are left. */
static bool peek_skip(uint32_t size, uint32_t *pos, uint64_t count) {
  if ((uint64_t)(size - *pos) < count) { return false; }
  *pos += (uint32_t)count;
  return true;
}

arnm_result grdw_confirmed_transaction_peek_id(const arnm_memory_block *serialized, uint64_t *out) {
  if (!serialized || !out || (serialized->size && !serialized->data)) {
    return ARNM_ERROR_NULL_POINTER;
  }
  const uint8_t *data = serialized->data;
  const uint32_t size = serialized->size;
  uint32_t pos = 0;
  uint64_t id = 0;

  while (pos < size) {
    uint64_t tag = 0;
    if (!peek_varint(data, size, &pos, &tag)) { return ARNM_ERROR_DECODE_FAILED; }
    const uint64_t field = tag >> 3;
    const uint32_t wire = (uint32_t)(tag & 7u);
    if (!field) { return ARNM_ERROR_DECODE_FAILED; }

    if (PEEK_ID_FIELD == field) {
      // the number itself; a later one replaces an earlier, as for any singular field
      if (PEEK_WIRE_VARINT != wire) { return ARNM_ERROR_DECODE_FAILED; }
      if (!peek_varint(data, size, &pos, &id)) { return ARNM_ERROR_DECODE_FAILED; }
      continue;
    }

    // any other field is stepped over as what its wire type says it is, never entered
    uint64_t length = 0;
    bool stepped = false;
    switch (wire) {
    case PEEK_WIRE_VARINT:
      stepped = peek_varint(data, size, &pos, &length);
      break;
    case PEEK_WIRE_FIXED64:
      stepped = peek_skip(size, &pos, 8u);
      break;
    case PEEK_WIRE_LENGTH:
      stepped = peek_varint(data, size, &pos, &length) && peek_skip(size, &pos, length);
      break;
    case PEEK_WIRE_FIXED32:
      stepped = peek_skip(size, &pos, 4u);
      break;
    default:
      stepped = false;
      break; // groups and reserved types have no place in this message
    }
    if (!stepped) { return ARNM_ERROR_DECODE_FAILED; }
  }

  *out = id;
  return ARNM_SUCCESS;
}
