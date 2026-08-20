#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_RUNTIME_COMPLETE_TRANSACTION_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_RUNTIME_COMPLETE_TRANSACTION_H

#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/timestamp.h"
#include "gradido_blockchain_core/data/types.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/ledger_anchor.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/types/address.h"
#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_blockchain_core/types/cross_group.h"
#include "gradido_blockchain_core/types/transaction.h"
#include "hostmem/memory_block.h"
#include "hostmem/multi_arena.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grdr_complete_transaction {
  uint64_t tx_nr;
  grdd_timestamp confirmed_at;
  grdd_timestamp created_at;
  uint8_t tx_community_uuid[HOSTMEM_UUID_BINARY_SIZE];
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
      uint8_t sender_pubkey[SIGN_PUBLIC_KEY_SIZE]; // set to 00000... on creation tx
      uint8_t recipient_pubkey[SIGN_PUBLIC_KEY_SIZE];
      grdd_unit amount;
      uint8_t coin_community_uuid[HOSTMEM_UUID_BINARY_SIZE];
    } transfer;
    struct {
      uint8_t user_public_key[SIGN_PUBLIC_KEY_SIZE];
      uint8_t name_hash[GENERIC_HASH_SIZE];
      uint8_t account_public_key[SIGN_PUBLIC_KEY_SIZE];
    } register_address;
    struct {
      uint8_t public_key[SIGN_PUBLIC_KEY_SIZE];
      uint8_t gmw_public_key[SIGN_PUBLIC_KEY_SIZE];
      uint8_t auf_public_key[SIGN_PUBLIC_KEY_SIZE];
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
  uint8_t tx_running_hash[GENERIC_HASH_SIZE];

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
  hostmem_memory_block body_bytes;

  // contains memory used for all pointer in this obj
  hostmem memory_area;

} grdr_complete_transaction;

typedef struct grdr_transaction_party grdr_transaction_party;

// will malloc memory for grdr_complete_transaction, for using with FFI
grdr_complete_transaction *grdr_complete_transaction_create();

// will set everything to null
void grdr_complete_transaction_init(grdr_complete_transaction *tx);
// will release memory and call init to set everything to null
void grdr_complete_transaction_release(grdr_complete_transaction *tx);
// call grdr_complete_transaction_release and will free memory where tx is pointing
void grdr_complete_transaction_free(grdr_complete_transaction *tx);

/**
 * @brief Decode a confirmed transaction and its body, and build the runtime transaction from
 *        both.
 *
 * Two decodes run here, the confirmed transaction and the body inside it, and they share
 * @p workspace: each copies out what it keeps before the next one starts. @p scratch holds
 * those copies and the wire structures built from them, and nothing that outlives the call --
 * the runtime transaction takes an arena of its own for what it keeps, so the chain is the
 * caller's to reset the moment this returns.
 *
 * A workspace that turns out too small comes back as HOSTMEM_ERROR_OUT_OF_MEMORY with nothing
 * else wrong: enlarge it and call again. A caller reading a stream of transactions learns the
 * size on the first one that does not fit and hands the same larger stretch to every call after
 * it -- which is why the size is here and not guessed inside. See @ref grdw_pb_workspace.
 *
 * @param[out]    tx              Runtime transaction to build; not NULL. Released first, so a
 *                                previously filled one is valid input.
 * @param[in]     serialized_data Protobuf bytes of a confirmed transaction; not NULL.
 * @param[in]     serialized_len  Their length; must be > 0.
 * @param[in]     community_uuid  16-byte UUID of the community context; not NULL.
 * @param[in]     workspace       Stretch pbtools works in; not NULL, 8 byte aligned. Written but
 *                                not read afterwards, and free to be reused.
 * @param[in,out] scratch         Chain the wire structures are built in; not NULL. Left holding
 *                                them on success and on failure alike.
 * @retval HOSTMEM_SUCCESS             @p tx is built.
 * @retval HOSTMEM_ERROR_NULL_POINTER  An argument is NULL.
 * @retval HOSTMEM_ERROR_INVALID_PARAM @p serialized_len is 0, or @p workspace is empty or not
 *                                     8 byte aligned.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY @p workspace was too small for one of the two messages.
 * @retval Anything the decode or grdm_complete_transaction_from_wire() reports.
 * @whisper Bytes are read once, and what they meant is kept
 */
hostmem_result grdr_complete_transaction_init_from_protobuf(
    grdr_complete_transaction *tx,
    const uint8_t *serialized_data,
    uint32_t serialized_len,
    const uint8_t community_uuid[16],
    const hostmem_memory_block *workspace,
    hostmem_multi_arena *scratch
);

const grdd_timestamp *grdr_complete_transaction_get_confirmed_at(
    const grdr_complete_transaction *tx
);
const grdd_timestamp *grdr_complete_transaction_get_created_at(const grdr_complete_transaction *tx);
const uint8_t *grdr_complete_transaction_get_tx_community_uuid(const grdr_complete_transaction *tx);
const grdw_ledger_anchor *grdr_complete_transaction_get_ledger_anchor(
    const grdr_complete_transaction *tx
);

const grdw_account_balance *grdr_complete_transaction_get_account_balance_for_public_key(
    const grdr_complete_transaction *tx, const uint8_t public_key[SIGN_PUBLIC_KEY_SIZE]
);
/**
 * @param return: pointer to 16 Byte Array with uuid or NULL
 */
const uint8_t *grdr_complete_transaction_get_sender_community_uuid(
    const grdr_complete_transaction *tx
);

/**
 * @param return: pointer to 16 Byte Array with uuid or NULL
 */
const uint8_t *grdr_complete_transaction_get_recipient_community_uuid(
    const grdr_complete_transaction *tx
);

/**
 * @param return: pointer to 32 Byte Array with public key or NULL
 */
const uint8_t *grdr_complete_transaction_get_sender_public_key(const grdr_complete_transaction *tx);

/**
 * @param return: pointer to 32 Byte Array with public key or NULL
 */
const uint8_t *grdr_complete_transaction_get_recipient_public_key(
    const grdr_complete_transaction *tx
);

const uint8_t *grdr_complete_transaction_get_registered_account(
    const grdr_complete_transaction *tx
);

grdt_transaction grdr_complete_transaction_get_transaction_type(
    const grdr_complete_transaction *tx
);

grdd_unit grdr_complete_transaction_get_amount(const grdr_complete_transaction *tx);

grdd_timestamp_seconds grdr_complete_transaction_get_target_date(
    const grdr_complete_transaction *tx
);
grdd_duration_seconds grdr_complete_transaction_get_timeout_duration(
    const grdr_complete_transaction *tx
);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_DATA_RUNTIME_COMPLETE_TRANSACTION_H
