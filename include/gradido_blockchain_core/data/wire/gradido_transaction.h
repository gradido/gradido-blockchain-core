#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_GRADIDO_TRANSACTION_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_GRADIDO_TRANSACTION_H

#include "basic_types.h"
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"
#include "ledger_anchor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct proto_gradido_gradido_transaction_t proto_gradido_gradido_transaction;

typedef struct grdw_gradido_transaction {
  grdw_signature_pair *sig_map;
  grd_memory_block body_bytes;
  grdw_ledger_anchor pairing_ledger_anchor;
  uint8_t sig_map_count;
} grdw_gradido_transaction;

void grdw_gradido_transaction_init(grdw_gradido_transaction *tx);

grd_result grdw_gradido_transaction_reserve_sig_map(
    grdw_gradido_transaction *tx, uint8_t sig_map_count, grd_memory *allocator
);
grd_result grdw_gradido_transaction_copy_sig_map(
    grdw_gradido_transaction *tx, const grdw_signature_pair *sig_map, uint8_t index
);
grd_result grdw_gradido_transaction_decode(
    grdw_gradido_transaction *tx, const grd_memory_block *binarySrc, grd_memory *allocator
);
grd_result grdw_gradido_transaction_encode(
    grd_memory_block *binaryDst,
    size_t *final_size,
    const grdw_gradido_transaction *tx,
    grd_memory *allocator
);

void grdw_gradido_transaction_free(grdw_gradido_transaction *tx, grd_memory *allocator);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_GRADIDO_TRANSACTION_H
