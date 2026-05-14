#ifndef GRADIDO_BLOCKCHAIN_C_DATA_WIRE_TRANSACTION_BODY_H
#define GRADIDO_BLOCKCHAIN_C_DATA_WIRE_TRANSACTION_BODY_H

#include "basic_types.h"
#include "gradido_blockchain_core/data/cross_group_type.h"
#include "gradido_blockchain_core/data/transaction_type.h"
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"
#include "specific_transactions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grdw_transaction_body {
  grdw_encrypted_memo *memos;
  uint8_t other_community_uuid[16];
  grdw_timestamp created_at;
  union {
    grdw_gradido_transfer transfer;
    grdw_gradido_creation creation;
    grdw_community_friends_update community_friends_update;
    grdw_register_address register_address;
    grdw_gradido_deferred_transfer deferred_transfer;
    grdw_community_root community_root;
    grdw_gradido_redeem_deferred_transfer redeem_deferred_transfer;
    grdw_gradido_timeout_deferred_transfer timeout_deferred_transfer;
  };
  grdd_transaction_type transaction_type;
  grdd_cross_group_type type;  
  uint8_t memos_count;
} grdw_transaction_body;

grd_result grdw_transaction_body_reserve_memos(grdw_transaction_body* body, size_t memos_count, grd_memory* allocator);

//! will move memo ptr, will overwrite memo if already exist at this index, need to be call grdw_transaction_body_reserve_memos first
//! @return GRD_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS if index is >= grdw_transaction_body.memos_count
grd_result grdw_transaction_body_move_memo(grdw_transaction_body* body, grdw_encrypted_memo* memo, uint8_t index);

//! will copy memo from ptr, allocate memory for memo, need to be call grdw_transaction_body_reserve_memos first
grd_result grdw_transaction_body_copy_memo(grdw_transaction_body* body, const grdw_encrypted_memo* memo, uint8_t index, grd_memory* allocator);

grd_result grdw_transaction_body_decode(grdw_transaction_body* body, const grd_memory_block* binarySrc, grd_memory* allocator);

#ifdef __cplusplus
}
#endif

#endif /* GRADIDO_BLOCKCHAIN_C_DATA_WIRE_TRANSACTION_BODY_H */
