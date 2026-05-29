#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_RUNTIME_COMPLETE_TRANSACTION_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_RUNTIME_COMPLETE_TRANSACTION_H

#include "gradido_blockchain_core/data/timestamp.h"
#include "gradido_blockchain_core/data/types.h"
#include "gradido_blockchain_core/data/wire/ledger_anchor.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/types/address.h"
#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_blockchain_core/types/cross_group.h"
#include "gradido_blockchain_core/types/transaction.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grdr_complete_transaction {
  uint64_t tx_nr;
  grdd_timestamp confirmed_at;
  grdd_timestamp created_at;
  uint8_t tx_community_uuid[16];
  grdw_ledger_anchor ledger_anchor;

  // --- Transaction Detail Data ---
  // Access based on 'transaction_type' (see end of struct):
  //   GRDT_TRANSACTION_CREATION                   -> transfer
  //   GRDT_TRANSACTION_TRANSFER                   -> transfer
  //   GRDT_TRANSACTION_DEFERRED_TRANSFER          -> transfer
  //   GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER   -> transfer
  //   GRDT_TRANSACTION_REGISTER_ADDRESS           -> register_address
  //   GRDT_TRANSACTION_COMMUNITY_ROOT             -> community_root
  union {
    struct {
      uint8_t sender_pubkey[32]; // set to 00000... on creation tx
      uint8_t recipient_pubkey[32];
      grdd_unit amount;
      uint8_t coin_community_uuid[16];
    } transfer;
    struct {
      uint8_t user_public_key[32];
      uint8_t name_hash[32];
      uint8_t account_public_key[32];
    } register_address;
    struct {
      uint8_t public_key[32];
      uint8_t gmw_public_key[32];
      uint8_t auf_public_key[32];
    } community_root;
  };

  // --- Transaction Context Data ---
  // Access based on 'transaction_type' (see end of struct):
  //   GRDT_TRANSACTION_CREATION                      -> target_date (not more than 2 months from
  //   created_at) GRDT_TRANSACTION_DEFERRED_TRANSFER -> timeout_duration
  //   GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER      -> previous_tx
  //   GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER     -> previous_tx
  //   GRDT_TRANSACTION_REGISTER_ADDRESS              -> address_type, derivation_index
  union {
    // target_date for creation gdd per month max calculation, not more than 2 months away from
    // create_at for creation transactions
    grdd_timestamp_seconds target_date;
    // timeout in seconds for deferred transfer
    grdd_duration_seconds timeout_duration;
    uint64_t previous_tx;
    struct {
      grdt_address address_type;
      uint32_t derivation_index;
    };
  };

  grdt_transaction transaction_type;
  grdt_balance_derivation balance_derivation_type;
  uint8_t tx_running_hash[32];

  // arrays
  grdw_account_balance *account_balances;
  size_t account_balances_count;
  grdw_encrypted_memo *encrypted_memos;
  size_t encrypted_memos_count;
  grdw_signature_pair *signature_pairs;
  size_t signature_pairs_count;

  // --- Cross-Group Data and Type ---
  // if not GRDT_CROSS_GROUP_LOCAL, following parameters may be set
  grdt_cross_group cross_group_type;
  uint8_t *tx_pairing_community_uuid;        // null on local txs
  grdw_ledger_anchor *pairing_ledger_anchor; // null on local txs

  // transaction body as protobuf serialization, payload for signature
  grd_memory_block body_bytes;

  // contains memory used for all pointer in this obj
  grd_memory memory_area;

} grdr_complete_transaction;

// will set everything to null
void grdr_complete_transaction_init(grdr_complete_transaction *tx);
// will release memory and call init to set everything to null
void grdr_complete_transaction_release(grdr_complete_transaction *tx);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_DATA_RUNTIME_COMPLETE_TRANSACTION_H
