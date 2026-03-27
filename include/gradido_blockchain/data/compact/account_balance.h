#ifndef GRADIDO_BLOCKCHAIN_C_COMPACT_ACCOUNT_BALANCE_H
#define GRADIDO_BLOCKCHAIN_C_COMPACT_ACCOUNT_BALANCE_H

#include <stdint.h>
#include "public_key_index.h"
#include "../../lib/unit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grdc_account_balance
{
  grdd_unit balance;
  grdc_public_key_index publicKeyIndex;
} grdc_account_balance;

#ifdef __cplusplus
}
#endif


#endif // GRADIDO_BLOCKCHAIN_C_COMPACT_ACCOUNT_BALANCE_H