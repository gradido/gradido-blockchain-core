#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/mapping/wire_from_pbtools.h"
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_transaction.h"

#include <string.h>

#define STATIC_BUFFER_SIZE 2048

grd_result grdw_gradido_transaction_reserve_sig_map(grdw_gradido_transaction* tx, uint8_t sig_map_count, grd_memory* allocator)
{
    if (!allocator || !tx) {
        return GRD_ERROR_NULL_POINTER;
    }
    if (!sig_map_count) {
        return GRD_ERROR_INVALID_PARAM;
    }
    grd_result result = grd_memory_buffer_alloc((uint8_t**)&tx->sig_map, allocator, sizeof(grdw_signature_pair) * sig_map_count);
    if (GRD_SUCCESS != result) {
        return result;
    }
    tx->sig_map_count = sig_map_count;
    return GRD_SUCCESS;
}

grd_result grdw_gradido_transaction_copy_sig_map(grdw_gradido_transaction* tx, const grdw_signature_pair* sig_map, uint8_t index)
{
    if (!tx || !sig_map) {
        return GRD_ERROR_NULL_POINTER;
    }
    if (index >= tx->sig_map_count) {
        return GRD_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS;
    }
    memcpy(&tx->sig_map[index], sig_map, sizeof(grdw_signature_pair));
    return GRD_SUCCESS;
}

grd_result grdw_gradido_transaction_decode(grdw_gradido_transaction* tx, const grd_memory_block* binarySrc, grd_memory* allocator)
{
    if (!tx || !binarySrc || !binarySrc->data) {
        return GRD_ERROR_NULL_POINTER;
    }
    if (!binarySrc->size) {
        return GRD_ERROR_INVALID_PARAM;
    }
    uint8_t buffer[STATIC_BUFFER_SIZE];
    //grd_memory_block buffer;
    //grd_memory_block_alloc(&buffer, allocator, 1024);
    struct proto_gradido_gradido_transaction_t *proto_tx;
    proto_tx = proto_gradido_gradido_transaction_new(buffer, STATIC_BUFFER_SIZE);
    if (!proto_tx) {
        return GRD_ERROR_STATIC_BUFFER_TO_SMALL;
    }
    int resultSize = proto_gradido_gradido_transaction_decode(proto_tx, binarySrc->data, binarySrc->size);
    if (resultSize < 0) {
        printf("proto_gradido_gradido_transaction_decode return size < 0: %d\n", resultSize);
        return GRD_ERROR_STATIC_BUFFER_TO_SMALL;
    }
    printf("pbtools: %d, src: %d, alloc: %d, overflow: %d\n", resultSize, binarySrc->size, allocator->last_index, allocator->out_of_memory_capacity);
    return grdm_gradido_transaction_from_pb(tx, proto_tx, allocator);
}

