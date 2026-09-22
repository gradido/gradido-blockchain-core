#ifndef GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_CHAIN_H
#define GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_CHAIN_H

#include "arnm/bucket_vector.h"
#include "gradido_blockchain_core/blockchain/addresses.h"
#include "gradido_blockchain_core/blockchain/store.h"
#include "gradido_blockchain_core/blockchain/transactions.h"
#include "gradido_blockchain_core/blockchain/transactions_filter.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/types/address.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup grdb_chain grdb_chain
 * @ingroup blockchain
 * @brief One chain, asked as one thing: its transactions indexed in segments, its addresses in
 *        a register of their own, and every question answered across the whole of it.
 *
 * A @ref grdb_transactions holds `[base_tx_nr, base_tx_nr + UINT32_MAX]` and refuses the number
 * after that rather than truncating it. That is a wall, not a slope: at 4 294 967 296
 * transactions a chain would stop, and no amount of memory would move it. A chain therefore
 * never has one index. It has a row of them, and a transaction goes to the one its number falls
 * in.
 *
 * ### One segment is still a row
 *
 * A chain of fifty thousand transactions has exactly one segment, and every call below still
 * goes through the merge. That is the point of building it this way: there is no unsegmented
 * mode to leave behind later, so there is no day on which the code that runs has to be swapped
 * for code that has never run. What answers at a billion transactions is what answers today,
 * with a different number in one field.
 *
 * ### The segment size changes no answer
 *
 * @ref grdb_chain_options::segment_transactions is a tuning figure, not a rule of the chain.
 * Segments partition a gapless run of numbers, so the transactions of a filter are the same
 * transactions however the run is cut, and a page is the same page: counted across every
 * segment, and read from the end it starts at, segment by segment. Two chains filled with the
 * same transactions under different segment sizes answer every call alike -- which is a
 * property a test can hold this code to, and @c test_blockchain_chain does.
 *
 * What the size does decide is how much of the index has to be resident to answer, once
 * segments can be dropped and rebuilt. Nothing here drops anything yet; the row is what makes
 * dropping possible without a single caller noticing.
 *
 * ### What is not segmented
 *
 * @ref grdb_addresses. It keeps transaction numbers as they are, in 64 bits, so it has no window
 * to run out of, and everything it answers is a point lookup that no merge would improve. One
 * register per chain, asked directly.
 *
 * ### Memory
 *
 * Everything comes from the `arnm *` given at init, segments included. A segment is opened when
 * the first transaction belonging to it arrives, never before, so an empty chain costs the
 * address register and an empty row.
 *
 * @note Nothing here is thread safe. Several threads may ask a chain that no thread is filling.
 *
 * @whisper A ledger bound in volumes, and the question read across every spine
 *
 * @{
 */

/**
 * @brief Transactions one segment holds when the options name no other number.
 *
 * Four million, which is about fifty megabytes of set blocks at the twelve bytes a transaction
 * costs -- small enough to be worth dropping one day, large enough that an ordinary chain is a
 * handful of them. It is not a property of the chain: two nodes running different sizes hold
 * the same transactions and give the same answers.
 */
#define GRDB_SEGMENT_TRANSACTIONS_DEFAULT (1u << 22)

/** @brief Shape of a chain. `{0}` is the default for every field. */
typedef struct grdb_chain_options {
  /** Transactions per segment; 0 takes @ref GRDB_SEGMENT_TRANSACTIONS_DEFAULT. */
  uint32_t segment_transactions;
  /** Addresses to make room for in each segment and in the register; 0 leaves them to grow. */
  uint32_t expected_addresses;
  /** Days to make room for in each segment; 0 leaves the day table to grow. */
  uint32_t expected_days;
  /**
   * Where the transactions themselves are kept; NULL for a chain that is only an index.
   *
   * Copied into the chain, so the struct need not outlive this call -- but whatever its
   * @c user_data points at has to outlive the chain.
   */
  const grdb_chain_store *store;
} grdb_chain_options;

/**
 * @brief A chain and everything indexed about it. Zeroed is not ready;
 *        @ref grdb_chain_init() makes it so.
 *
 * The fields are public to be read and measured, and written only by the calls below.
 */
typedef struct grdb_chain {
  arnm *source;                  /**< Where memory comes from; NULL is the host. */
  grdb_chain_store store;        /**< Where the transactions are; `{0}` for an index alone. */
  arnm_bvec segments;            /**< @ref grdb_transactions, oldest first, no gaps. */
  grdb_addresses addresses;      /**< What the chain says about its addresses. */
  uint32_t segment_transactions; /**< Numbers one segment covers. */
  uint32_t expected_addresses;   /**< Passed to every segment opened. */
  uint32_t expected_days;        /**< Passed to every segment opened. */
  uint64_t first_segment;        /**< Number of the first segment, `first_tx_nr / size`. */
  uint64_t min_tx_nr;            /**< Smallest transaction number held; 0 while empty. */
  uint64_t max_tx_nr;            /**< Largest transaction number held; 0 while empty. */
  uint64_t transaction_count;    /**< Transactions added. */
  bool ready;                    /**< Init ran and release did not. */
} grdb_chain;

// ********** building *******************

/**
 * @brief Prepare an empty chain over @p source. Opens no segment.
 *
 * @param[out]    chain   Chain to prepare; not NULL. Overwritten whole; uninitialised is fine.
 * @param[in]     options Shape; NULL gives the defaults.
 * @param[in,out] source  Allocator for everything the chain holds; NULL for the host.
 * @retval ARNM_SUCCESS             Ready and empty.
 * @retval ARNM_ERROR_NULL_POINTER  @p chain is NULL.
 * @retval ARNM_ERROR_OUT_OF_MEMORY @p source had no room for the first tables.
 * @warning Calling this on a chain that still holds memory leaks it. Release it first.
 * @whisper An empty shelf, and the first volume not yet opened
 */
arnm_result grdb_chain_init(grdb_chain *chain, const grdb_chain_options *options, arnm *source);

/**
 * @brief Give every byte back to @p source and leave the chain as init found it.
 * @param[in,out] chain Chain to empty; NULL is a no-op.
 */
void grdb_chain_release(grdb_chain *chain);

/**
 * @brief Forget every transaction, keep what memory can be kept for the next fill.
 *
 * The row of segments is given back -- a fresh fill may begin at another number and cut them
 * differently -- while the address register keeps its tables.
 *
 * @param[in,out] chain Chain to empty; NULL is a no-op.
 * @whisper The shelf cleared, the bindings kept
 */
void grdb_chain_reset(grdb_chain *chain);

/**
 * @brief Add one confirmed transaction to the segment its number belongs to, and to the
 *        address register. Its number must be the one after the last.
 *
 * The transaction is read, never held: nothing in @p tx has to outlive the call. A number that
 * opens a new segment opens it here; the segment before it is left as it stands and is never
 * written again.
 *
 * @param[in,out] chain Chain to add to; not NULL and initialised.
 * @param[in]     tx    The transaction; not NULL. Its @c tx_nr must be one above the last one
 *                      added -- the first one sets where the chain begins -- and must not be 0.
 * @retval ARNM_SUCCESS                    Indexed, in the segment and in the register.
 * @retval ARNM_ERROR_NULL_POINTER         @p chain or @p tx is NULL.
 * @retval ARNM_ERROR_INVALID_STATE        @p chain is not initialised.
 * @retval ARNM_ERROR_INVALID_PARAM        @c tx_nr is 0, or is not the number after the last.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE    The transaction or address type is not one this
 *                                        library knows.
 * @retval ARNM_ERROR_OUT_OF_MEMORY        A segment, a set or the register found no room. What
 *                                        was written before this transaction stands; what this
 *                                        transaction had already written stands with it, and
 *                                        adding it again after making room completes it.
 * @whisper The number decides the volume, and the volume opens itself
 */
arnm_result grdb_chain_add(grdb_chain *chain, const grdr_complete_transaction *tx);

// ********** the transactions themselves *******************

/**
 * @brief Point at transaction @p tx_nr, read through the chain's store.
 *
 * What the index never answers: the index says *which* numbers, this says what is under one.
 * The transaction belongs to the store and is good until the next call on it -- long enough for
 * the call that asked, which is what a validation needs and all it needs.
 *
 * @param[in]  chain Chain to read from; may be NULL.
 * @param[in]  tx_nr The number to read.
 * @param[out] out   Receives the address; not NULL. Untouched unless the answer is success.
 * @retval ARNM_SUCCESS                          @p *out points at it.
 * @retval ARNM_ERROR_NULL_POINTER               @p out is NULL.
 * @retval ARNM_ERROR_INVALID_STATE              @p chain is NULL, not initialised, or carries
 *                                               no store.
 * @retval ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS  The store holds no such number.
 * @return Anything else the store answered.
 * @whisper The number looked up, and the page itself turned to
 */
arnm_result grdb_chain_fetch(
    const grdb_chain *chain, uint64_t tx_nr, const grdr_complete_transaction **out
);

/**
 * @brief Read `[from_tx_nr, to_tx_nr]` out of the store and index every one of them.
 *
 * What a node does at start: the chain is the truth, the index is rebuilt from it. Each
 * transaction is read where the store keeps it and handed to @ref grdb_chain_add(), which reads
 * it and keeps nothing -- so a chain of any length costs what one transaction costs, and nothing
 * is copied along the way.
 *
 * @param[in,out] chain      Chain to fill; not NULL, initialised, and carrying a store.
 * @param[in]     from_tx_nr First number to read; 0 means 1. Must continue the chain where it
 *                           already holds transactions.
 * @param[in]     to_tx_nr   Last number to read; 0 means "until the store has no more".
 * @param[out]    loaded     Receives how many were indexed; may be NULL. Written on failure
 *                           too, so a caller can see how far it got.
 * @retval ARNM_SUCCESS                          The range was read, or the store ran out with
 *                                               @p to_tx_nr left open.
 * @retval ARNM_ERROR_INVALID_STATE              @p chain is NULL, not initialised, or carries
 *                                               no store.
 * @retval ARNM_ERROR_INVALID_PARAM              @p to_tx_nr is below @p from_tx_nr.
 * @retval ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS  A number the caller named is not in the store.
 * @return Anything else the store or the index answered, at the transaction it happened on.
 * @whisper The ledger read from the beginning, and the register written as it goes
 */
arnm_result grdb_chain_load(
    grdb_chain *chain, uint64_t from_tx_nr, uint64_t to_tx_nr, uint64_t *loaded
);

/** @brief Whether transactions can be appended to this chain's store. */
static inline bool grdb_chain_is_writable(const grdb_chain *chain) {
  return chain && chain->ready && grdb_chain_store_is_writable(&chain->store);
}

// ********** asking *******************

/**
 * @brief How many transactions of the whole chain match @p filter.
 *
 * Every segment is counted and the counts are added: a segment the filter cannot touch answers
 * zero without reading a set.
 *
 * @param[in]  chain  Chain to ask; not NULL and initialised.
 * @param[in]  filter What to look for; not NULL.
 * @param[out] out    Receives the count; not NULL. Untouched on failure.
 * @retval ARNM_SUCCESS             Counted; 0 when nothing matches.
 * @retval ARNM_ERROR_NULL_POINTER  An argument is NULL.
 * @retval ARNM_ERROR_INVALID_STATE @p chain is not initialised.
 * @whisper Every volume asked, and only the sum written down
 */
arnm_result grdb_chain_count(
    const grdb_chain *chain, const grdb_transactions_filter *filter, uint64_t *out
);

/**
 * @brief One page of what @p filter matches anywhere in the chain, and how many there are.
 *
 * @p skip and @p size are counted over the **whole** chain, not inside a segment: the walk runs
 * the segments from the end the page starts at, spends the skip against each segment's matches
 * before it reads any, and stops writing once the page is full -- but keeps counting, because
 * the count answers for the filter and not for the page.
 *
 * @param[in]  chain      Chain to ask; not NULL and initialised.
 * @param[in]  filter     What to look for; not NULL.
 * @param[in]  skip       Matches to pass over first, from the end the page starts at.
 * @param[in]  size       Most numbers to write, up to @ref GRDB_PAGE_MAX.
 * @param[in]  descending Start at the newest match and go back.
 * @param[out] out        Room for @p size numbers; not NULL unless @p size is 0.
 * @param[out] written    Receives how many were written; not NULL. Untouched on failure.
 * @param[out] count      Receives the matches in the whole chain; not NULL. Untouched on
 *                        failure.
 * @retval ARNM_SUCCESS             Answered; @p *written may be 0 with a @p *count above it when
 *                                 the page starts past the last match.
 * @retval ARNM_ERROR_NULL_POINTER  An argument is NULL.
 * @retval ARNM_ERROR_INVALID_PARAM @p size is above @ref GRDB_PAGE_MAX.
 * @retval ARNM_ERROR_INVALID_STATE @p chain is not initialised.
 * @whisper A page counted off across the volumes, not inside one
 */
arnm_result grdb_chain_listing(
    const grdb_chain *chain,
    const grdb_transactions_filter *filter,
    uint32_t skip,
    uint32_t size,
    bool descending,
    uint64_t *out,
    uint32_t *written,
    uint64_t *count
);

/**
 * @brief The newest transaction of the chain that matches @p filter.
 *
 * The segments from the newest back, and the first that answers ends the walk -- what a
 * validation asks, and usually one segment deep.
 *
 * @param[in]  chain  Chain to ask; may be NULL.
 * @param[in]  filter What to look for; may be NULL.
 * @param[out] out    Receives the number; not NULL. Untouched when nothing matches.
 * @return false when nothing matches, when an argument is NULL, or when @p chain is not
 *         initialised.
 * @whisper The last entry that still bears this name, in whichever volume it fell
 */
bool grdb_chain_newest(
    const grdb_chain *chain, const grdb_transactions_filter *filter, uint64_t *out
);

/**
 * @brief The span of transaction numbers confirmed inside `[from_second, to_second]`.
 *
 * The smallest of the segments' answers and the largest, which is the chain's span because the
 * segments' spans lie end to end.
 *
 * @param[in]  chain       Chain to read; may be NULL.
 * @param[in]  from_second Earliest second, 0 for the first transaction.
 * @param[in]  to_second   Latest second, 0 for the last.
 * @param[out] min         Receives the first number of the span; not NULL.
 * @param[out] max         Receives the last; not NULL.
 * @return false when no transaction was confirmed in that span, leaving both untouched.
 * @whisper Days named, and the shelf answers with the pages they cover
 */
bool grdb_chain_range_of_days(
    const grdb_chain *chain, int64_t from_second, int64_t to_second, uint64_t *min, uint64_t *max
);

// ********** what the chain says about an address *******************

/** @brief The kind of account @p public_key is, as the chain last said. */
grdt_address grdb_chain_address_type(const grdb_chain *chain, const uint8_t *public_key);

/** @brief The kind of account @p public_key was as of transaction @p at_tx_nr. */
bool grdb_chain_address_type_at(
    const grdb_chain *chain, const uint8_t *public_key, uint64_t at_tx_nr, grdt_address *out
);

/** @brief The last transaction that carried a balance entry for @p public_key. */
bool grdb_chain_address_last_balance(
    const grdb_chain *chain, const uint8_t *public_key, uint64_t *out
);

/** @brief Whether the chain holds anything at all about @p public_key. */
bool grdb_chain_knows_address(const grdb_chain *chain, const uint8_t *public_key);

// ********** reading what is in it *******************

/** @brief Transactions added. 0 for NULL or an empty chain. */
static inline uint64_t grdb_chain_size(const grdb_chain *chain) {
  return chain ? chain->transaction_count : 0u;
}

/** @brief Segments the chain holds. 0 for NULL or an empty chain. */
uint32_t grdb_chain_segment_count(const grdb_chain *chain);

/**
 * @brief The segment at @p position, oldest first, or NULL when there is none.
 *
 * For a caller that measures rather than asks -- what a segment's pool lent, how many addresses
 * it saw. Nothing an answer depends on is read through here.
 */
const grdb_transactions *grdb_chain_segment(const grdb_chain *chain, uint32_t position);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_CHAIN_H
