#ifndef GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_STORE_H
#define GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_STORE_H

#include "arnm/memory_block.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/result.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup grdb_chain_store grdb_chain_store
 * @ingroup blockchain
 * @brief Where a chain's transactions are kept, behind two function pointers.
 *
 * Everything a chain is asked -- which transactions match a filter, how many, in what order,
 * what an address is -- is answered from the indices, in memory, by the same code whatever
 * holds the chain. What is left over is one question: **give me the transaction with this
 * number**. That is this interface, and it is why a store is a struct with two pointers rather
 * than a class with twenty methods.
 *
 * Three kinds of store have been imagined and two are written here. A chain kept in memory
 * (@ref grdb_in_memory), a chain a host holds in a database or a file and hands over across a
 * language boundary (@ref grdb_chain_ffi), and a node's own block files. The third one is the
 * second one with a different reader behind it: from here they are indistinguishable, which is
 * the point.
 *
 * ### A pointer, not a copy
 *
 * @ref grdr_complete_transaction cannot be copied, and is not meant to be: it carries its own
 * allocator handle, so it **moves** cleanly, and what is passed around is the pointer. A fetch
 * therefore hands back an address into the store rather than filling a struct of the caller's:
 *
 * ```c
 * const grdr_complete_transaction *tx = NULL;
 * if (ARNM_SUCCESS == grdb_chain_store_fetch(store, 4711, &tx)) {
 *   // read it here, inside the call that asked for it
 * }
 * ```
 *
 * **The transaction belongs to the store.** The caller never releases it, and must not keep the
 * pointer past the next call on that store: a store that holds its chain decoded hands out the
 * same address for as long as it holds it, while one that has to decode first keeps a single
 * place to decode into and overwrites it. A caller that needs a transaction beyond that asks
 * again -- which, where the chain is already decoded, is a lookup and nothing more.
 *
 * Appending goes the other way round and moves: on success the store has taken the transaction
 * and the caller's struct is left empty. @c serialized beside it is for a store that writes
 * bytes down rather than keeping objects -- a host across a language boundary, a block file --
 * and such a store refuses an append that brings none.
 *
 * ### What a store does not do
 *
 * It does not validate, number, or hash. It is told a number and given bytes, and it keeps
 * them in that order. What may be appended, and what the running hash over it is, is decided
 * above -- in the chain, where the protocol lives.
 *
 * @note A store is a plain value: it holds a pointer to whatever implements it and is copied
 *       freely. What it points at has to outlive every chain that carries a copy.
 *
 * @whisper Two doors in a wall: one to ask through, one to set something down
 *
 * @{
 */

/**
 * @brief The two calls a chain makes of whatever holds its transactions.
 *
 * `{0}` is no store at all, which a chain accepts: an index filled by hand needs none.
 */
typedef struct grdb_chain_store {
  /** Passed back to both calls; whatever implements this store. */
  void *user_data;

  /**
   * @brief Point at transaction @p tx_nr. Required.
   *
   * @param[in,out] user_data As given in this struct.
   * @param[in]     tx_nr     The number to read.
   * @param[out]    out       Receives the address of the transaction, which belongs to the
   *                          store. Valid until the next call on this store. Untouched unless
   *                          the answer is @c ARNM_SUCCESS.
   * @retval ARNM_SUCCESS                          @p *out points at it.
   * @retval ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS  This store holds no such number.
   * @return Anything else when the read failed.
   */
  arnm_result (*fetch)(void *user_data, uint64_t tx_nr, const grdr_complete_transaction **out);

  /**
   * @brief Take @p tx as transaction @p tx_nr. NULL makes the store read only.
   *
   * @param[in,out] user_data  As given in this struct.
   * @param[in]     tx_nr      The number to keep it under; one above the store's largest.
   * @param[in,out] tx         The transaction, or NULL when @p serialized carries it instead.
   *                           On @c ARNM_SUCCESS the store has taken it and @p tx is left empty
   *                           -- the caller neither uses nor releases it afterwards. On any
   *                           failure it is untouched and still theirs.
   * @param[in]     serialized The `ConfirmedTransaction` bytes, or NULL when the caller has
   *                           none. A store that writes bytes down needs them and refuses
   *                           without; one that keeps objects ignores them. Not kept past the
   *                           call.
   * @retval ARNM_SUCCESS             Taken.
   * @retval ARNM_ERROR_INVALID_PARAM @p tx_nr is not the number after the store's largest, or
   *                                  this store needed @p serialized and got none.
   * @retval ARNM_ERROR_NULL_POINTER  Both @p tx and @p serialized are missing.
   * @return Anything else when the write failed; @p tx is still the caller's.
   */
  arnm_result (*append)(
      void *user_data,
      uint64_t tx_nr,
      grdr_complete_transaction *tx,
      const arnm_memory_block *serialized
  );
} grdb_chain_store;

/** @brief Whether @p store can be written to. A read only chain has no @c append. */
static inline bool grdb_chain_store_is_writable(const grdb_chain_store *store) {
  return store && store->append;
}

/** @brief Whether @p store can be read at all, which is what makes it a store. */
static inline bool grdb_chain_store_is_readable(const grdb_chain_store *store) {
  return store && store->fetch;
}

/**
 * @brief @ref grdb_chain_store::fetch with the checks, for a caller holding a store directly.
 *
 * @param[in]  store Store to read; may be NULL.
 * @param[in]  tx_nr The number to read.
 * @param[out] out   Receives the address; not NULL. See the lifetime note on the module.
 * @retval ARNM_ERROR_NULL_POINTER  @p out is NULL.
 * @retval ARNM_ERROR_INVALID_STATE @p store is NULL or has no @c fetch.
 * @return Otherwise whatever the store answered.
 */
arnm_result grdb_chain_store_fetch(
    const grdb_chain_store *store, uint64_t tx_nr, const grdr_complete_transaction **out
);

/**
 * @brief @ref grdb_chain_store::append with the checks.
 *
 * @param[in]     store      Store to write to; may be NULL.
 * @param[in]     tx_nr      The number to keep it under.
 * @param[in,out] tx         The transaction, or NULL when @p serialized carries it. Taken on
 *                           success, untouched on failure.
 * @param[in]     serialized Its bytes, or NULL when @p tx carries it.
 * @retval ARNM_ERROR_NULL_POINTER  Both are missing.
 * @retval ARNM_ERROR_INVALID_STATE @p store is NULL or read only.
 * @return Otherwise whatever the store answered.
 */
arnm_result grdb_chain_store_append(
    const grdb_chain_store *store,
    uint64_t tx_nr,
    grdr_complete_transaction *tx,
    const arnm_memory_block *serialized
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_STORE_H
