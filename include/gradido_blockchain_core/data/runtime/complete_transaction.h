#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_RUNTIME_COMPLETE_TRANSACTION_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_RUNTIME_COMPLETE_TRANSACTION_H

#include "gradido_blockchain_core/data/timestamp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grdr_complete_transaction {
  // ===== identity =====
  uint64_t tx_id;
  uint8_t tx_running_hash[32];
} grdr_complete_transaction;

#ifdef __cplusplus
}
#endif

#endif //GRADIDO_BLOCKCHAIN_CORE_DATA_RUNTIME_COMPLETE_TRANSACTION_H
