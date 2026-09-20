#ifndef GRADIDO_BLOCKCHAIN_CORE_INDEX_TRANSACTIONS_H
#define GRADIDO_BLOCKCHAIN_CORE_INDEX_TRANSACTIONS_H

#include "arnm/bucket_vector.h"
#include "arnm/converter.h"
#include "arnm/graded_block_pool.h"
#include "arnm/key_map.h"
#include "arnm/roaring_bitmap.h"
#include "arnm/roaring_query.h"
#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/index/transactions_filter.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/types/transaction.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup index Indices over a chain */

/**
 * @defgroup grdx_transactions grdx_transactions
 * @ingroup index
 * @brief Which transactions of a chain match a filter, answered from sets of transaction
 *        numbers instead of from the transactions themselves.
 *
 * A chain is read far more often than it grows, and almost every read is a question about a
 * few transactions out of all of them: the ones an address took part in, the ones of a type, the
 * ones inside a span of days. Answering those by walking the chain and deserialising each
 * transaction is what this index exists to avoid. It keeps, per chain:
 *
 * - three sets per address -- the transactions that **changed its balance**, that it **signed**,
 *   and that **named it** without either;
 * - one set per transaction type;
 * - one set per foreign coin community, holding the transactions that carry a balance in a coin
 *   that is not the chain's own;
 * - the largest transaction number of every day, so a span of dates becomes a span of numbers.
 *
 * A @ref grdx_transactions_filter is answered by intersecting the sets it names inside that
 * span of numbers, counted and paged without a result set ever being built
 * (@ref arnm_roaring_query). Nothing here reads a transaction again: the answer is transaction
 * numbers, and fetching those is the caller's.
 *
 * ### What it holds, and what it does not
 *
 * Only what a filter needs. Amounts, memos, signatures, balances -- everything a caller reads
 * once it knows *which* transactions to fetch -- stay in the chain. The index also keeps no
 * transaction data it could not rebuild: it is filled at start from the chain and never written
 * to disk, which the numbers allow -- fifty thousand transactions load and deserialise in about
 * 41 ms, and the index takes 13 ns per address entry on top.
 *
 * ### Transaction numbers
 *
 * Sets hold `uint32_t`, the API speaks the `uint64_t` of a transaction. An index covers
 * `[base_tx_nr, base_tx_nr + UINT32_MAX]` and stores `tx_nr - base_tx_nr`; a number outside is
 * refused rather than truncated. One index per chain with base 0 holds 4.29 billion
 * transactions; an index per block file uses that file's first number as its base.
 *
 * Transactions arrive one after the other, each numbered one above the one before: a chain
 * numbers a transaction as it completes it, so the numbers run without gaps. The index holds
 * callers to that -- a number that is not the next one is refused -- and reads it back: a
 * filter that names no set at all is every number of the span it asks for, counted and paged
 * without a set being touched.
 *
 * ### Memory
 *
 * Everything comes from the `arnm *` given at init: the address map, the per address sets, and
 * the pool the set blocks are cut from. Pass NULL for the host allocator, or an arena chain to
 * keep the whole index inside it. @ref grdx_transactions_release() gives every block back;
 * @ref grdx_transactions_reset() empties the index and keeps the memory for the next fill.
 *
 * @note Nothing here is thread safe. Several threads may ask an index that no thread is filling.
 *
 * @whisper Every transaction leaves three marks -- who paid, who signed, who was named -- and
 *          the marks are what is searched, not the ledger
 *
 * @{
 */

/**
 * @brief Largest number of foreign coin communities one index keeps a set for.
 *
 * Eight, because a filter for "this chain's own coin only" asks for a transaction in **none** of
 * the foreign sets, and @ref ARNM_ROARING_QUERY_MAX is how many sets one list of a query holds.
 * A chain pairing with more than eight foreign coins would need that filter answered another
 * way; until one does, a ninth is refused where it would be silently dropped.
 */
#define GRDX_COIN_COMMUNITY_MAX 8u

/** @brief Most transaction numbers one call writes into a page. */
#define GRDX_PAGE_MAX 1000u

/** @brief Shape of an index. `{0}` is the default: base 0, defaults for everything else. */
typedef struct grdx_transactions_options {
  /** First transaction number the index can hold. A smaller number is refused. */
  uint64_t base_tx_nr;
  /** Addresses to make room for before the first add; 0 leaves the map to grow on its own. */
  uint32_t expected_addresses;
  /** Days to make room for; 0 leaves the day table to grow. One entry is 4 bytes. */
  uint32_t expected_days;
} grdx_transactions_options;

/** @brief The three sets of one address. Read through the calls below, never written directly. */
typedef struct grdx_address_sets {
  arnm_roaring_bitmap balance; /**< Transactions that changed this address's balance. */
  arnm_roaring_bitmap signed_; /**< Transactions this address signed. */
  arnm_roaring_bitmap other;   /**< Transactions that named it without either. */
} grdx_address_sets;

/**
 * @brief One chain's index. Zeroed is not ready; @ref grdx_transactions_init() makes it so.
 *
 * The fields are public because the containers behind them are, and because a caller may want
 * to measure them. They are written only by the calls below.
 */
typedef struct grdx_transactions {
  arnm *source;                /**< Where memory comes from; NULL is the host. */
  arnm_graded_block_pool pool; /**< The blocks every set is built from. */
  arnm_key_map addresses;      /**< 32 byte public key -> dense address id. */
  arnm_bvec address_sets;      /**< @ref grdx_address_sets at that id. */
  arnm_bvec day_max_tx;        /**< uint32_t per day since @c first_day, 0 for a day without. */
  arnm_roaring_bitmap per_type[GRDT_TRANSACTION_COUNT];        /**< Transactions of each type. */
  arnm_roaring_bitmap coin_community[GRDX_COIN_COMMUNITY_MAX]; /**< Per foreign coin community. */
  uint8_t coin_community_uuid[GRDX_COIN_COMMUNITY_MAX][ARNM_UUID_BINARY_SIZE]; /**< Their uuids. */
  uint8_t chain_community_uuid[ARNM_UUID_BINARY_SIZE]; /**< The chain's own, from the first tx. */
  uint32_t coin_community_count;                       /**< Foreign coin communities seen. */
  uint64_t base_tx_nr;                                 /**< Transaction number stored as 0. */
  uint64_t min_tx_nr;         /**< Smallest transaction number indexed; 0 while empty. */
  uint64_t max_tx_nr;         /**< Largest transaction number indexed; 0 while empty. */
  int64_t first_day;          /**< Day of the first transaction, in days since the epoch. */
  uint32_t transaction_count; /**< Transactions added. */
  bool ready;                 /**< Init ran and release did not. */
} grdx_transactions;

// ********** building *******************

/**
 * @brief Prepare an empty index over @p source. Allocates the address map's first table only.
 *
 * @param[out]    index   Index to prepare; not NULL. Overwritten whole; uninitialised is fine.
 * @param[in]     options Shape; NULL gives the defaults.
 * @param[in,out] source  Allocator for everything the index holds; NULL for the host.
 * @retval ARNM_SUCCESS                  Ready and empty.
 * @retval ARNM_ERROR_NULL_POINTER       @p index is NULL.
 * @retval ARNM_ERROR_OUT_OF_MEMORY      @p source had no room for the first tables.
 * @warning Calling this on an index that still holds memory leaks it. Release it first.
 * @whisper An empty register, its columns already ruled
 */
arnm_result grdx_transactions_init(
    grdx_transactions *index, const grdx_transactions_options *options, arnm *source
);

/**
 * @brief Give every block back to @p source and leave the index as init found it.
 * @param[in,out] index Index to empty; NULL is a no-op.
 */
void grdx_transactions_release(grdx_transactions *index);

/**
 * @brief Forget every transaction, keep the memory for the next fill.
 *
 * What @ref grdx_transactions_release() and @ref grdx_transactions_init() together
 * would do, without giving the blocks back: the sets empty, the address map clears, the day
 * table empties, and the next add starts a fresh chain at the same base.
 *
 * @param[in,out] index Index to empty; NULL is a no-op.
 * @whisper The register wiped clean, its paper kept
 */
void grdx_transactions_reset(grdx_transactions *index);

/**
 * @brief Add one confirmed transaction: its addresses in their roles, its type, its foreign
 *        coins, and its day. Its number must be the one after the last.
 *
 * The transaction is read, never held: nothing in @p tx has to outlive the call.
 *
 * Which address takes which role follows the transaction itself -- a signature pair makes a
 * signer, an account balance entry makes a balance change, and a public key named in the body
 * with neither is the third role. An address in several roles is in several sets. A zero public
 * key -- the sender of a creation -- is no address and is skipped.
 *
 * @param[in,out] index Index to add to; not NULL and initialised.
 * @param[in]     tx    The transaction; not NULL. Its @c tx_nr must be one above the last one
 *                      added -- the first one sets where the index begins -- and its
 *                      @c confirmed_at must not be before the first transaction's day.
 * @retval ARNM_SUCCESS                       Indexed; every set that gained a number grew.
 * @retval ARNM_ERROR_NULL_POINTER            @p index or @p tx is NULL.
 * @retval ARNM_ERROR_INVALID_STATE           @p index is not initialised.
 * @retval ARNM_ERROR_INVALID_PARAM           @c tx_nr is below the base, or is not the number
 *                                           after the last one added, or the day is before the
 *                                           first transaction's day.
 * @retval ARNM_ERROR_RESOURCE_SIZE_EXCEED    @c tx_nr is past `base + UINT32_MAX`.
 * @retval ARNM_ERROR_RESOURCE_EXHAUSTED      A ninth foreign coin community appeared; see
 *                                           @ref GRDX_COIN_COMMUNITY_MAX.
 * @retval ARNM_ERROR_OUT_OF_MEMORY           A set, the map or the day table found no block. The
 *                                           index keeps every transaction added before, and the
 *                                           sets this one had already entered keep it -- add it
 *                                           again after making room and the duplicate is a
 *                                           no-op, since a number already the largest is one.
 * @whisper Three marks pressed into the register, and the day noted at the margin
 */
arnm_result grdx_transactions_add(grdx_transactions *index, const grdr_complete_transaction *tx);

// ********** asking *******************

/**
 * @brief How many transactions match @p filter.
 *
 * @param[in]  index  Index to ask; not NULL and initialised.
 * @param[in]  filter What to look for; not NULL.
 * @param[out] out    Receives the count; not NULL. Untouched on failure.
 * @retval ARNM_SUCCESS             Counted; 0 when nothing matches.
 * @retval ARNM_ERROR_NULL_POINTER  An argument is NULL.
 * @retval ARNM_ERROR_INVALID_STATE @p index is not initialised.
 * @whisper Counted without reading a single entry
 */
arnm_result grdx_transactions_count(
    const grdx_transactions *index, const grdx_transactions_filter *filter, uint64_t *out
);

/**
 * @brief One page of what @p filter matches, and how many there are, from a single pass.
 *
 * The page that a listing asks for: @p size numbers after passing over @p skip, newest first
 * when @p descending. @p count always answers for the whole filter, however small the page --
 * the two together are what an API call needs, and they cost one walk, not two.
 *
 * @param[in]  index      Index to ask; not NULL and initialised.
 * @param[in]  filter     What to look for; not NULL.
 * @param[in]  skip       Matches to pass over first, from the end the page starts at.
 * @param[in]  size       Most numbers to write, up to @ref GRDX_PAGE_MAX.
 * @param[in]  descending Start at the newest match and go back.
 * @param[out] out        Room for @p size numbers; not NULL unless @p size is 0.
 * @param[out] written    Receives how many were written; not NULL. Untouched on failure.
 * @param[out] count      Receives the matches in the whole filter; not NULL. Untouched on
 *                        failure.
 * @retval ARNM_SUCCESS              Answered; @p *written may be 0 with a @p *count above it when
 *                                  the page starts past the last match.
 * @retval ARNM_ERROR_NULL_POINTER   An argument is NULL.
 * @retval ARNM_ERROR_INVALID_PARAM  @p size is above @ref GRDX_PAGE_MAX.
 * @retval ARNM_ERROR_INVALID_STATE  @p index is not initialised.
 * @whisper A page torn from the middle, and the thickness of the book in the same glance
 */
arnm_result grdx_transactions_listing(
    const grdx_transactions *index,
    const grdx_transactions_filter *filter,
    uint32_t skip,
    uint32_t size,
    bool descending,
    uint64_t *out,
    uint32_t *written,
    uint64_t *count
);

/**
 * @brief The newest transaction that matches @p filter.
 *
 * What a validation asks: the last transaction of this address, of this type, before that
 * number. A page of one, read from the newest end, which touches one key of one set.
 *
 * @param[in]  index  Index to ask; not NULL and initialised.
 * @param[in]  filter What to look for; not NULL.
 * @param[out] out    Receives the number; not NULL. Untouched when nothing matches.
 * @return false when nothing matches, when an argument is NULL, or when @p index is not
 *         initialised -- a question a caller answers the same way in each case.
 * @whisper The last entry that still bears this name
 */
bool grdx_transactions_newest(
    const grdx_transactions *index, const grdx_transactions_filter *filter, uint64_t *out
);

// ********** reading what is in it *******************

/** @brief Transactions added. 0 for NULL or an empty index. */
static inline uint32_t grdx_transactions_size(const grdx_transactions *index) {
  return index ? index->transaction_count : 0u;
}

/** @brief Addresses seen. 0 for NULL or an empty index. */
uint32_t grdx_transactions_address_count(const grdx_transactions *index);

/**
 * @brief The three sets of @p public_key, or NULL when the index never saw it.
 *
 * For a caller that wants to read the sets itself -- a count of all of an address's
 * transactions, its newest number -- without going through a filter.
 *
 * @param[in] index      Index to read; may be NULL.
 * @param[in] public_key 32 bytes; may be NULL.
 * @whisper The three columns kept under one name
 */
const grdx_address_sets *grdx_transactions_address(
    const grdx_transactions *index, const uint8_t *public_key
);

/**
 * @brief The span of transaction numbers confirmed inside `[from_seconds, to_seconds]`.
 *
 * The day table asked directly: every transaction of those days lies inside the span, and the
 * span holds nothing from before or after them. What @ref grdx_transactions_count() and
 * its siblings apply to a filter that names dates.
 *
 * @param[in]  index       Index to read; not NULL.
 * @param[in]  from_second Earliest second, 0 for the first transaction.
 * @param[in]  to_second   Latest second, 0 for the last.
 * @param[out] min         Receives the first number of the span; not NULL.
 * @param[out] max         Receives the last; not NULL.
 * @return false when no transaction was confirmed in that span, leaving both untouched.
 * @whisper Days named, and the register answers with the pages they cover
 */
bool grdx_transactions_range_of_days(
    const grdx_transactions *index,
    int64_t from_second,
    int64_t to_second,
    uint64_t *min,
    uint64_t *max
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_INDEX_TRANSACTIONS_H
