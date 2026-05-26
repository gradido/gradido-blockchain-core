#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_BASIC_TYPES_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_BASIC_TYPES_H

#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/types/memo_key.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// basic types
typedef struct grdw_account_balance {
  uint8_t pubkey[32];
  int64_t balance;
  uint8_t community_uuid[16];
} grdw_account_balance;

typedef struct grdw_encrypted_memo {
  grdt_memo_key type;
  grd_memory_block memo;
} grdw_encrypted_memo;

typedef struct grdw_signature_pair {
  uint8_t public_key[32];
  uint8_t signature[64];
} grdw_signature_pair;

typedef struct grdw_timestamp_seconds {
  int64_t seconds;
} grdw_timestamp_seconds;

typedef struct grdw_transfer_amount {
  uint8_t pubkey[32];
  int64_t amount;
  uint8_t community_uuid[16];
} grdw_transfer_amount;

#ifdef __cplusplus
}
#endif

#endif /* GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_BASIC_TYPES_H */
