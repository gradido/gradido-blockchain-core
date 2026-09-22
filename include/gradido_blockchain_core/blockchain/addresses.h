#ifndef GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_ADDRESSES_H
#define GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_ADDRESSES_H

#include "arnm/bucket_vector.h"
#include "arnm/key_map.h"
#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/types/address.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup grdb_addresses grdb_addresses
 * @ingroup blockchain
 * @brief What a chain says about an address itself: what kind of account it is, since when, and
 *        when its balance last moved.
 *
 * Two questions stand in front of almost every validation, and neither is about a set of
 * transactions:
 *
 * - **What kind of address is this?** A register address transaction gives an address its type,
 *   a community root gives the two community accounts theirs, and a deferred transfer creates a
 *   recipient that needs no registration. Validation asks this twice per transfer and once per
 *   signer of a creation, and it asks it *as of* a transaction number, because a transaction is
 *   validated against the chain as it was when it arrived.
 * - **When did this address's balance last change?** The answer is the starting point of every
 *   balance calculation, and it is one number.
 *
 * Both are answered here without touching a set: the type from the transactions that set it,
 * kept newest first per address, and the last balance change as a single number per address.
 * @ref grdb_transactions answers everything that is a *filter* over many transactions; this
 * answers what is already decided about one address.
 *
 * ### What it holds
 *
 * Per address, and only for addresses a transaction says something about: the transactions that
 * gave it a type, each with the type it gave, and the largest transaction number that carried a
 * balance entry for it. Nothing else -- no amounts, no balances, no memos.
 *
 * Transactions arrive in ascending number order, the order a chain grows in.
 *
 * ### Memory
 *
 * Everything comes from the `arnm *` given at init: the address map, one entry per address, and
 * one record per type giving transaction. An address usually has exactly one of those, so the
 * records are a short chain and walking it back to a transaction number is a step or two.
 *
 * @note Nothing here is thread safe. Several threads may ask an index that no thread is filling.
 *
 * @whisper An address carries two dates: the day it was given its name, and the day its balance
 *          last moved
 *
 * @{
 */

/** @brief Shape of an address index. `{0}` is the default. */
typedef struct grdb_addresses_options {
  /** Addresses to make room for before the first add; 0 leaves the map to grow on its own. */
  uint32_t expected_addresses;
} grdb_addresses_options;

/**
 * @brief What a chain knows about its addresses. Zeroed is not ready;
 *        @ref grdb_addresses_init() makes it so.
 *
 * The fields are public to be read and measured, and written only by the calls below.
 */
typedef struct grdb_addresses {
  arnm *source;               /**< Where memory comes from; NULL is the host. */
  arnm_key_map keys;          /**< 32 byte public key -> dense address id. */
  arnm_bvec entries;          /**< One record per address, at its id. */
  arnm_bvec type_changes;     /**< Every transaction that gave an address a type. */
  uint64_t max_tx_nr;         /**< Largest transaction number added; 0 while empty. */
  uint32_t transaction_count; /**< Transactions that said anything about an address. */
  bool ready;                 /**< Init ran and release did not. */
} grdb_addresses;

// ********** building *******************

/**
 * @brief Prepare an empty index over @p source.
 *
 * @param[out]    index   Index to prepare; not NULL. Overwritten whole; uninitialised is fine.
 * @param[in]     options Shape; NULL gives the defaults.
 * @param[in,out] source  Allocator for everything the index holds; NULL for the host.
 * @retval ARNM_SUCCESS             Ready and empty.
 * @retval ARNM_ERROR_NULL_POINTER  @p index is NULL.
 * @retval ARNM_ERROR_OUT_OF_MEMORY @p source had no room for the first tables.
 * @warning Calling this on an index that still holds memory leaks it. Release it first.
 * @whisper A register of names, still blank
 */
arnm_result grdb_addresses_init(
    grdb_addresses *index, const grdb_addresses_options *options, arnm *source
);

/**
 * @brief Give every byte back to @p source and leave the index as init found it.
 * @param[in,out] index Index to empty; NULL is a no-op.
 */
void grdb_addresses_release(grdb_addresses *index);

/**
 * @brief Forget every address, keep the memory for the next fill.
 * @param[in,out] index Index to empty; NULL is a no-op.
 */
void grdb_addresses_reset(grdb_addresses *index);

/**
 * @brief Read what @p tx says about addresses: a type given, a balance moved.
 *
 * Which is which follows the transaction:
 * - a **community root** gives its GMW key @ref GRDT_ADDRESS_COMMUNITY_GMW and its AUF key
 *   @ref GRDT_ADDRESS_COMMUNITY_AUF;
 * - a **register address** gives both the user key and the account key the type the transaction
 *   carries;
 * - a **deferred transfer** gives its recipient @ref GRDT_ADDRESS_DEFERRED_TRANSFER, the one
 *   type an address receives without being registered;
 * - every **account balance entry** moves that address's last balance change to this number.
 *
 * A transaction that says none of this -- a plain transfer, a creation -- is counted and
 * otherwise passes through. Nothing in @p tx has to outlive the call.
 *
 * @param[in,out] index Index to add to; not NULL and initialised.
 * @param[in]     tx    The transaction; not NULL. Its @c tx_nr must be above every number added
 *                      before.
 * @retval ARNM_SUCCESS                    Read.
 * @retval ARNM_ERROR_NULL_POINTER         @p index or @p tx is NULL.
 * @retval ARNM_ERROR_INVALID_STATE        @p index is not initialised.
 * @retval ARNM_ERROR_INVALID_PARAM        @c tx_nr is not above the largest already added.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE    The transaction or address type is not one this
 *                                         library knows.
 * @retval ARNM_ERROR_OUT_OF_MEMORY        The map, the entries or the records found no room.
 *                                         Whatever the transaction had already written stays.
 * @whisper A name written in, or a date moved forward
 */
arnm_result grdb_addresses_add(grdb_addresses *index, const grdr_complete_transaction *tx);

// ********** asking *******************

/**
 * @brief The kind of account @p public_key is, as the chain last said.
 *
 * @param[in] index      Index to ask; may be NULL.
 * @param[in] public_key 32 bytes; may be NULL.
 * @return The type, or @ref GRDT_ADDRESS_NONE when no transaction ever gave this key one --
 *         which is also the answer for an address that only ever sent and received.
 * @whisper What the register says today
 */
grdt_address grdb_addresses_type(const grdb_addresses *index, const uint8_t *public_key);

/**
 * @brief The kind of account @p public_key was as of transaction @p at_tx_nr.
 *
 * What a validation needs: a transaction is judged against the chain as it stood when it
 * arrived, not as it stands now. Walks the transactions that gave this address a type, newest
 * first, and answers with the first one at or below @p at_tx_nr. An address almost always has
 * exactly one, so this is a comparison, not a search.
 *
 * @param[in]  index      Index to ask; may be NULL.
 * @param[in]  public_key 32 bytes; may be NULL.
 * @param[in]  at_tx_nr   The transaction to answer as of, that transaction included.
 * @param[out] out        Receives the type; not NULL. Untouched when the answer is false.
 * @return false when nothing at or below @p at_tx_nr gave this address a type.
 * @whisper The register as it read that day, not today
 */
bool grdb_addresses_type_at(
    const grdb_addresses *index, const uint8_t *public_key, uint64_t at_tx_nr, grdt_address *out
);

/**
 * @brief The transactions that gave @p public_key a type, newest first.
 *
 * Usually one. More than one means the address was registered again -- moved, or re-typed --
 * and a caller that has to explain a type rather than just use it reads them all.
 *
 * @param[in]  index      Index to ask; may be NULL.
 * @param[in]  public_key 32 bytes; may be NULL.
 * @param[out] out        Room for @p size numbers; may be NULL when @p size is 0.
 * @param[in]  size       Most numbers to write.
 * @return How many were written, @p size or fewer.
 * @whisper Every day this name was written anew
 */
uint32_t grdb_addresses_type_changes(
    const grdb_addresses *index, const uint8_t *public_key, uint64_t *out, uint32_t size
);

/**
 * @brief The last transaction that carried a balance entry for @p public_key.
 *
 * Where a balance calculation starts. For a number *before* the newest one, this index has
 * nothing to say -- ask @ref grdb_transactions_newest() with the balance changing role and an
 * upper bound, which reads one key of one set.
 *
 * @param[in]  index      Index to ask; may be NULL.
 * @param[in]  public_key 32 bytes; may be NULL.
 * @param[out] out        Receives the number; not NULL. Untouched when the answer is false.
 * @return false when no transaction ever moved this address's balance.
 * @whisper The last day the account was touched
 */
bool grdb_addresses_last_balance(
    const grdb_addresses *index, const uint8_t *public_key, uint64_t *out
);

/** @brief Whether the index holds anything at all about @p public_key. */
bool grdb_addresses_knows(const grdb_addresses *index, const uint8_t *public_key);

/** @brief Addresses the index holds something about. 0 for NULL or an empty index. */
uint32_t grdb_addresses_size(const grdb_addresses *index);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_ADDRESSES_H
