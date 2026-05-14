#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_HIERO_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_HIERO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gradido_blockchain_core/result.h"
#include "basic_types.h"

#include <stdint.h>

// hiero
typedef struct grdw_hiero_account_id {
  int64_t shardNum;
  int64_t realmNum;
  int64_t accountNum;
} grdw_hiero_account_id;

typedef struct grdw_hiero_transaction_id {
  grdw_timestamp transactionValidStart;
  grdw_hiero_account_id accountID;
} grdw_hiero_transaction_id;


#ifdef __cplusplus
}
#endif

#endif  /* GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_HIERO_H */
