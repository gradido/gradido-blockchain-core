#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_CONFIRMED_TRANSACTION_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_CONFIRMED_TRANSACTION_H

#include "basic_types.h"
#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/wire/pb_workspace.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_transaction.h"
#include "hostmem/memory_block.h"
#include "hostmem/multi_arena.h"
#include "ledger_anchor.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grdw_confirmed_transaction grdw_confirmed_transaction
 *  @ingroup wire
 *  @brief Confirmed transaction with ledger state and account balances
 *  @{
 */

/**
 * @brief Transaction confirmed by the network with ledger state snapshot.
 *
 * Contains the confirmed transaction, its ID, confirmation timestamp, running hash,
 * ledger anchor, and the resulting account balances. The transaction has settled
 * into the blockchain, carrying the state after consensus.
 *
 */
typedef struct grdw_confirmed_transaction {
  //! Unique Transaction Nr of the confirmed transaction (auto increment, without holes)
  uint64_t id;
  //! The gradido transaction that was confirmed
  grdw_gradido_transaction transaction;
  //! Timestamp when the transaction reached consensus in hiero blockchain, may be earlier then
  //! created_at
  grdd_timestamp confirmed_at;
  //! hash of this transaction, including runnning_hash from previous transaction (id - 1 )
  uint8_t running_hash[GENERIC_HASH_SIZE];
  //! Ledger anchor linking to the hiero transaction id oder db id on imported legacy transactions
  grdw_ledger_anchor ledger_anchor;
  //! Array of account balances after the transaction applied, account which where changed from this
  //! transaction
  grdw_account_balance *account_balances;
  //! Number of account balance entries in the array.
  uint8_t account_balances_count;
  //! Method used to derive the balance changes. For example extern for legacy but usually node
  grdt_balance_derivation balance_derivation;
} grdw_confirmed_transaction;

/**
 * @brief Initialize a confirmed transaction to a clean, empty state.
 *
 * Sets all pointers to null and numeric fields to zero. The transaction emerges
 * ready to receive its confirmation data and ledger state.
 *
 * @param[out] tx  Confirmed transaction to initialize.
 */
void grdw_confirmed_transaction_init(grdw_confirmed_transaction *tx);

/**
 * @brief Reserve memory for a given number of account balance entries.
 *
 * Allocates an array of account balance pointers using the provided allocator.
 * This must be called before any copy_account_balance operations. The array
 * breathes into existence, holding space for balance snapshots.
 *
 * @param[in/out] tx                       Confirmed transaction to reserve balances in.
 * @param[in]     account_balances_count   Number of balance slots to allocate.
 * @param[in]     allocator                Memory allocator for the pointer array.
 * @return                                 HOSTMEM_SUCCESS on success
 *                                         HOSTMEM_ERROR_OUT_OF_MEMORY if allocator hasn't enough
 * space
 */
hostmem_result grdw_confirmed_transaction_reserve_account_balances(
    grdw_confirmed_transaction *tx, uint8_t account_balances_count, hostmem_multi_arena *allocator
);

/**
 * @brief Copy an account balance into a reserved slot.
 *
 * Deep-copies the account balance structure into the transaction at the specified
 * index. The caller must have called reserve_account_balances first. The allocator
 * is used for the copy.
 *
 * @param[in/out] tx               Confirmed transaction to receive the balance copy.
 * @param[in]     account_balance  Source account balance to copy from.
 * @param[in]     index            Target slot index.
 * @return                         HOSTMEM_SUCCESS on success, error code on failure.
 */
hostmem_result grdw_confirmed_transaction_copy_account_balance(
    grdw_confirmed_transaction *tx, grdw_account_balance *account_balance, uint8_t index
);

/**
 * @brief Decode a confirmed transaction from binary wire format.
 *
 * Parses the binary representation and populates the transaction structure,
 * including the nested gradido transaction, ledger anchor, and all account
 * balances. Requires an area allocator for nested allocations. The binary
 * stream settles into confirmed form.
 *
 * @param[out] tx       Confirmed transaction to populate.
 * @param[in]  binary_src Source memory block containing binary data.
 * @param[in]  allocator Area allocator for nested allocations.
 * @return              HOSTMEM_SUCCESS on success
 *                      HOSTMEM_ERROR_OUT_OF_MEMORY if allocator hasn't enough space
 * @note                The allocator must be an area allocator; memory is
 *                      not freed individually but as a whole.
 * @whisper             Settlement takes shape
 */
hostmem_result grdw_confirmed_transaction_decode(
    grdw_confirmed_transaction *tx,
    const hostmem_memory_block *binary_src,
    const hostmem_memory_block *workspace,
    hostmem_multi_arena *allocator
);

/**
 * @brief Encode a confirmed transaction into binary wire format.
 *
 * Serializes the confirmed transaction structure, including the gradido
 * transaction, ledger anchor, and all account balances, into a compact binary
 * representation. The encoded form travels across the network, carrying
 * the settled state between nodes.
 *
 * @param[out] binary_dst   Destination memory block for encoded data.
 * @param[out] final_size  Number of bytes written to binary_dst.
 * @param[in]  tx          Confirmed transaction to encode.
 * @param[in]  allocator   Memory allocator for temporary encoding buffers.
 * @return                 HOSTMEM_SUCCESS on success
 *                         HOSTMEM_ERROR_DESTINATION_BUFFER_TO_SMALL if binary_dst is to small
 *                         HOSTMEM_ERROR_OUT_OF_MEMORY if allocator hasn't enough space
 * @whisper                Settlement becomes message
 */
hostmem_result grdw_confirmed_transaction_encode(
    hostmem_memory_block *binary_dst,
    int *final_size,
    const grdw_confirmed_transaction *tx,
    const hostmem_memory_block *workspace
);

/**
 * @brief Free all memory owned by a confirmed transaction.
 *
 * Releases the account balances array and the nested gradido transaction, which carries its
 * own signature map and body bytes. The ledger anchor is not among them: it is a discriminator
 * and a union of two by-value members, with nothing allocated to give back. The transaction
 * dissolves back to a clean state, ready for reuse or destruction.
 *
 * @param[in,out] tx        Confirmed transaction to free; may be NULL, which does nothing.
 *                          Otherwise it must be one this module filled and that nothing has
 *                          edited since: each member's recorded size is what the chain is told
 *                          to take back, and a size that does not match its allocation moves
 *                          the bump index by the wrong amount and hands the same bytes out
 *                          twice.
 * @param[in]     allocator The chain the members were taken from; NULL does nothing at all --
 *                          not even the re-initialisation below -- and a chain other than the
 *                          one they came from reclaims nothing.
 *
 * Members are released in reverse allocation order, because a bump chain can only take back its
 * own tail. Even then the reclaim is best effort: anything that is no longer the tail stays
 * reserved until hostmem_multi_arena_reset(), which is how memory really comes back from a
 * chain. Nothing is returned, because whether the bytes came back is not something a caller can
 * act on -- on every path that gets past the two checks above, the structure is left reading as
 * freshly initialised.
 */
void grdw_confirmed_transaction_free(
    grdw_confirmed_transaction *tx, hostmem_multi_arena *allocator
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_CONFIRMED_TRANSACTION_H
