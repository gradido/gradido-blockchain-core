#ifndef GRADIDO_BLOCKCHAIN_C_COMPACT_CONFIRMED_TRANSACTION_COMPACT_H
#define GRADIDO_BLOCKCHAIN_C_COMPACT_CONFIRMED_TRANSACTION_COMPACT_H

#include <stdint.h>
#include "account_balance.h"
#include "public_key_index.h"
#include "tx_id.h"
#include "tx_timestamps.h"
#include "../address_type.h"
#include "../balance_derivation_type.h"
#include "../cross_group_type.h"
#include "../timestamp.h"
#include "../transaction_type.h"
#include "../../lib/unit.h"

#ifdef __cplusplus
extern "C" {
#endif

// Gradido Compact Confirmed Transaction
typedef struct grdc_confirmed_tx 
{
    grdc_tx_timestamps timestamps;

    // inline Timestamp getConfirmedAt() const { return timestamps.getConfirmedAt(); }
    // inline Timestamp getCreatedAt() const { return timestamps.getCreatedAt(); }

    // txId and pairingTxId packed to save 8 Bytes padding
    uint64_t txNrs[2];
    uint32_t communityIdIndex[2];
    // inline TxId getTxId() const { return TxId(txNrs[0], communityIdIndex[0]); }
    // for cross group transactions, else empty
    // inline TxId getPairingTxId() const { return TxId(txNrs[1], communityIdIndex[1]); }
    
    // account balances and memos via txId in separate list/manager/thingy
    
    // enums, usually uint8_t
    uint8_t crossGroupType; // grdd_cross_group_type
    uint8_t transactionType; // grdd_transaction_type
    uint8_t balanceDerivationType; // grdd_balance_derivation_type
    uint8_t accountBalanceCount;

    grdc_account_balance accountBalances[2];

    // common fields for most transactions
    union { // 24 Bytes
      struct {
        grdd_unit amount; // 8 Bytes
        grdc_public_key_index recipientPublicKeyIndex; // 8 Bytes
        uint64_t targetDate; // 8 Bytes
      } creation;
      struct {
        grdd_unit amount; // 8 Bytes
        grdc_public_key_index senderPublicKeyIndex; // 8 Bytes
        grdc_public_key_index recipientPublicKeyIndex; // 8 Bytes
      } transfer; // also used for redeem deferred transfer, and deferredTransferTransactionNr is stored in extra dictionary
      struct {
        grdd_unit amount; // 8 Bytes
        // work only on local, take communityIdIndex from txId
        uint32_t senderPublicKeyIndex; // 4 Bytes
        uint32_t recipientPublicKeyIndex; // 4 Bytes
        uint32_t timeoutDuration; // 4 Bytes
      } deferredTransfer; // fund deferred transfer address only on your own community
      struct {
        grdc_tx_id deferredTransferTransactionNr; // 16 Bytes, contain 4 Bytes padding
      } timeoutDeferredTransfer;
      struct {
        uint8_t addressType; // grdd_address_type // 1 Byte
        uint16_t derivationIndex; // 2 Byte (for the time beeing, update if more than 65535 are needed) 
        uint32_t nameHashIndex; // 4 Bytes
        grdc_public_key_index userPublicKeyIndex; // 8 Bytes
        grdc_public_key_index accountPublicKeyIndex; // 8 Bytes
      } registerAddress;
      struct {
        grdc_public_key_index communityPublicKeyIndex; // 8 Bytes
        grdc_public_key_index communityAufPublicKeyIndex; // 8 Bytes
        grdc_public_key_index communityGmwPublicKeyIndex; // 8 Bytes
      } communityRoot;
    };        
} grdc_confirmed_tx;

inline grdd_timestamp grdc_confirmed_tx_get_confirmed_at(const grdc_confirmed_tx* c) {
  return grdc_tx_timestamps_get_confirmed_at(&c->timestamps);
}

inline grdd_timestamp grdc_confirmed_tx_get_created_at(const grdc_confirmed_tx* c) {
  return grdc_tx_timestamps_get_created_at(&c->timestamps);
}

inline grdc_tx_id grdc_confirmed_tx_get_tx_id(const grdc_confirmed_tx* c) {
  grdc_tx_id txId{.nr = c->txNrs[0], .communityIdIndex = c->communityIdIndex[0]};
  return txId;
}

inline grdc_tx_id grdc_confirmed_tx_get_pairing_tx_id(const grdc_confirmed_tx* c) {
  grdc_tx_id pairingTxId{.nr = c->txNrs[1], .communityIdIndex = c->communityIdIndex[1]};
  return pairingTxId;
}

#ifdef __cplusplus
}
#endif


#endif // GRADIDO_BLOCKCHAIN_C_COMPACT_CONFIRMED_TRANSACTION_COMPACT_H