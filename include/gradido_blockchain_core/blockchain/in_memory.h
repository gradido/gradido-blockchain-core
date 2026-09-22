#ifndef GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_IN_MEMORY_H
#define GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_IN_MEMORY_H

#include "arnm/bucket_vector.h"
#include "arnm/converter.h"
#include "arnm/memory.h"
#include "gradido_blockchain_core/blockchain/store.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup grdb_in_memory grdb_in_memory
 * @ingroup blockchain
 * @brief A chain that lives nowhere but here: the serialized transactions in a vector, handed
 *        back by number.
 *
 * The store for a test, a benchmark, and a chain small enough to hold whole. It keeps the bytes
 * it was appended -- one copy per transaction, drawn from the allocator it was given -- and
 * decodes them again on every fetch.
 *
 * ### It keeps them decoded
 *
 * Appending **moves** a transaction in -- @ref grdr_complete_transaction carries its own
 * allocator handle, so the struct travels and what it points at stays where it is -- and a
 * fetch hands back that address. Nothing is decoded again, and nothing is copied, because a
 * transaction cannot be copied and does not need to be.
 *
 * A caller that has bytes rather than an object gives them instead: the store decodes them once,
 * on the way in. Either way, reading is a lookup.
 *
 * ### Memory
 *
 * The record vector, every transaction's bytes, and the scratch the decoder works in all come
 * from the `arnm *` given at init. @ref grdb_in_memory_release() gives all of it back.
 *
 * @note Not thread safe. Several threads may read a store that no thread is appending to.
 *
 * @whisper A chain held in the hand, weighed each time it is read
 *
 * @{
 */

/** @brief Bytes the decoder works in when the options name no other size. */
#define GRDB_IN_MEMORY_SCRATCH_DEFAULT (256u * 1024u)

/** @brief Shape of a store. `{0}` is the default for every field. */
typedef struct grdb_in_memory_options {
  /** Transactions to make room for before the first append; 0 leaves the vector to grow. */
  uint32_t expected_transactions;
  /** Scratch for one decode; 0 takes @ref GRDB_IN_MEMORY_SCRATCH_DEFAULT. Rounded up to a
   *  multiple of 8, which is what the decoder wants of it. */
  uint32_t scratch_bytes;
} grdb_in_memory_options;

/**
 * @brief A chain's transactions, serialized, in order. Zeroed is not ready;
 *        @ref grdb_in_memory_init() makes it so.
 */
typedef struct grdb_in_memory {
  arnm *source;                                  /**< Where memory comes from; NULL is the host. */
  arnm_bvec records;                             /**< One transaction per slot, in order. */
  uint8_t *scratch;                              /**< Where a decode on the way in works. */
  uint32_t scratch_size;                         /**< Its size, a multiple of 8. */
  uint8_t community_uuid[ARNM_UUID_BINARY_SIZE]; /**< The chain's community, given at init. */
  uint64_t first_tx_nr;                          /**< Number of the first append; 0 while empty. */
  uint64_t max_tx_nr;                            /**< Number of the last; 0 while empty. */
  bool ready;                                    /**< Init ran and release did not. */
} grdb_in_memory;

/**
 * @brief Prepare an empty store over @p source.
 *
 * @param[out]    store          Store to prepare; not NULL. Overwritten whole.
 * @param[in]     options        Shape; NULL gives the defaults.
 * @param[in]     community_uuid 16 bytes a transaction decoded on the way in carries; not NULL.
 *                               Copied.
 * @param[in,out] source         Allocator for everything; NULL for the host.
 * @retval ARNM_SUCCESS             Ready and empty.
 * @retval ARNM_ERROR_NULL_POINTER  @p store or @p community_uuid is NULL.
 * @retval ARNM_ERROR_OUT_OF_MEMORY @p source had no room for the vector or the scratch.
 * @warning Calling this on a store that still holds memory leaks it. Release it first.
 * @whisper An empty ledger, and a desk to read it at
 */
arnm_result grdb_in_memory_init(
    grdb_in_memory *store,
    const grdb_in_memory_options *options,
    const uint8_t community_uuid[ARNM_UUID_BINARY_SIZE],
    arnm *source
);

/**
 * @brief Give every byte back to @p source and leave the store as init found it.
 * @param[in,out] store Store to empty; NULL is a no-op.
 */
void grdb_in_memory_release(grdb_in_memory *store);

/**
 * @brief Release every transaction it took, keep the vector and the scratch for the next fill.
 * @param[in,out] store Store to empty; NULL is a no-op.
 */
void grdb_in_memory_reset(grdb_in_memory *store);

/**
 * @brief The @ref grdb_chain_store that reads and writes @p store.
 *
 * A plain value pointing at @p store, which therefore has to outlive every chain it is given
 * to. A NULL or unready @p store gives a store with no calls in it, which every chain refuses.
 *
 * @param[in] store Store to wrap; may be NULL.
 * @whisper The handle fitted to the drawer
 */
grdb_chain_store grdb_in_memory_as_store(grdb_in_memory *store);

/** @brief Transactions kept. 0 for NULL or an empty store. */
uint64_t grdb_in_memory_size(const grdb_in_memory *store);

/**
 * @brief The transaction numbered @p tx_nr, or NULL when the store holds no such number.
 *
 * The same address @ref grdb_chain_store::fetch gives, for a caller holding the store itself.
 * It belongs to the store and lives as long as the store holds it.
 */
const grdr_complete_transaction *grdb_in_memory_at(const grdb_in_memory *store, uint64_t tx_nr);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_IN_MEMORY_H
