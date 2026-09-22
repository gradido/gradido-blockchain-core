#ifndef GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_TRANSACTIONS_FILTER_H
#define GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_TRANSACTIONS_FILTER_H

#include "arnm/converter.h"
#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/types/transaction.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup grdb_transactions_filter grdb_transactions_filter
 * @ingroup blockchain
 * @brief What to look for in a chain: an address in a role, a type, a coin, a span of numbers,
 *        a span of days -- the question @ref grdb_transactions answers.
 *
 * A filter is a plain value. It is built, asked with, and forgotten; it holds no memory of its
 * own and points at nothing. A zeroed filter matches every transaction of a chain, and every
 * part that is set narrows the answer -- they narrow it together, never as alternatives.
 *
 * ### Why the bytes are copied
 *
 * A public key and a community uuid live **inside** the filter, not behind a pointer into the
 * caller's memory. A filter is often built in one language and asked in another, where a
 * borrowed pointer outlives nothing: a temporary buffer, a moved object, a garbage collector
 * doing its work between building the filter and asking with it. 48 bytes copied once per
 * query is the price of a filter that cannot dangle.
 *
 * ### Building one
 *
 * In C, a designated initialiser says it all, and the setters below are there for a caller that
 * validates as it goes:
 *
 * ```c
 * grdb_transactions_filter filter = {0};
 * grdb_transactions_filter_set_address(&filter, key, GRDB_ADDRESS_ROLE_INVOLVED);
 * grdb_transactions_filter_set_transaction_type(&filter, GRDT_TRANSACTION_TRANSFER);
 * ```
 *
 * Across an FFI boundary the setters are the whole interface: @ref
 * grdb_transactions_filter_create() allocates one, the setters fill it, @ref
 * grdb_transactions_filter_free() gives it back, and no host needs to know this struct's layout or
 * size.
 *
 * ### What "unset" is
 *
 * Zero, everywhere. No role means no address is asked for, and the key is then not read at all.
 * An all zero uuid is no uuid -- the nil uuid names no community. A bound of 0 is no bound,
 * which a chain allows because it numbers its transactions from 1 and no transaction is
 * confirmed at second 0.
 *
 * @whisper A question shaped before it is asked, carrying its own paper
 *
 * @{
 */

/** @brief How an address took part in a transaction. A filter names one, a transaction has any. */
typedef enum grdb_address_role {
  GRDB_ADDRESS_ROLE_NONE = 0, /**< No address named: the filter asks over the whole chain. */
  GRDB_ADDRESS_ROLE_INVOLVED, /**< Any of the three: balance changed, signed, or only named. */
  GRDB_ADDRESS_ROLE_BALANCE   /**< Only where the address's balance changed. */
} grdb_address_role;

/**
 * @brief What to look for. `{0}` matches every transaction of the chain.
 *
 * The fields are public and may be read and written directly by a caller that knows this
 * header; the calls below are the same thing with the checks, and the only way in for a host
 * that does not.
 */
typedef struct grdb_transactions_filter {
  /** The address to look for; read only when @c role names one. All zero otherwise. */
  uint8_t public_key[SIGN_PUBLIC_KEY_SIZE];
  /**
   * The coin community to look for; all zero for any coin. The chain's own uuid asks for the
   * transactions that carry **no** foreign coin balance, another uuid for the ones that carry
   * that coin.
   */
  uint8_t coin_community_uuid[ARNM_UUID_BINARY_SIZE];
  /** Which of the address's three sets to read. */
  grdb_address_role role;
  /** One type, or @ref GRDT_TRANSACTION_NONE for any. */
  grdt_transaction transaction_type;
  /** Smallest transaction number to match; 0 for no bound. */
  uint64_t min_tx_nr;
  /** Largest transaction number to match; 0 for no bound. */
  uint64_t max_tx_nr;
  /** Earliest confirmation second to match; 0 for no bound. */
  int64_t from_seconds;
  /** Latest confirmation second to match, that second included; 0 for no bound. */
  int64_t to_seconds;
} grdb_transactions_filter;

// ********** a filter of one's own *******************

/**
 * @brief Make @p filter match everything. The same as zeroing it.
 * @param[out] filter Filter to empty; NULL is a no-op.
 */
void grdb_transactions_filter_init(grdb_transactions_filter *filter);

/**
 * @brief Allocate a filter that matches everything, for a caller that cannot place one itself.
 *
 * For FFI: the host holds the pointer and never needs this struct's size or layout. A C caller
 * writes `grdb_transactions_filter filter = {0};` instead and allocates nothing.
 *
 * @return The filter, or NULL when the allocation failed. Give it back with
 *         @ref grdb_transactions_filter_free().
 * @whisper A blank sheet handed across the border
 */
grdb_transactions_filter *grdb_transactions_filter_create(void);

/**
 * @brief Free a filter @ref grdb_transactions_filter_create() handed out.
 * @param[in] filter Filter to free; NULL is a no-op.
 */
void grdb_transactions_filter_free(grdb_transactions_filter *filter);

// ********** what to look for *******************

/**
 * @brief Look for @p public_key in @p role.
 *
 * The 32 bytes are copied into the filter; @p public_key is not kept.
 *
 * @param[in,out] filter     Filter to narrow; not NULL.
 * @param[in]     public_key 32 bytes; NULL only together with @ref GRDB_ADDRESS_ROLE_NONE.
 * @param[in]     role       Which of the address's three sets to read.
 *                           @ref GRDB_ADDRESS_ROLE_NONE clears the address again.
 * @retval ARNM_SUCCESS               Set.
 * @retval ARNM_ERROR_NULL_POINTER    @p filter is NULL, or @p public_key is NULL with a role.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE @p role is not one of @ref grdb_address_role.
 * @whisper One name, and the part it played
 */
arnm_result grdb_transactions_filter_set_address(
    grdb_transactions_filter *filter, const uint8_t *public_key, grdb_address_role role
);

/**
 * @brief Look only for transactions of @p type.
 *
 * @param[in,out] filter Filter to narrow; not NULL.
 * @param[in]     type   One type, or @ref GRDT_TRANSACTION_NONE for any again.
 * @retval ARNM_SUCCESS                 Set.
 * @retval ARNM_ERROR_NULL_POINTER      @p filter is NULL.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE @p type is not one this library knows.
 */
arnm_result grdb_transactions_filter_set_transaction_type(
    grdb_transactions_filter *filter, grdt_transaction type
);

/**
 * @brief Look only for transactions that carry a balance in @p uuid's coin.
 *
 * The 16 bytes are copied. Naming the chain's own community asks for the other side of it: the
 * transactions that carry no foreign coin at all.
 *
 * @param[in,out] filter Filter to narrow; not NULL.
 * @param[in]     uuid   16 bytes, or NULL for any coin again.
 * @retval ARNM_SUCCESS              Set.
 * @retval ARNM_ERROR_NULL_POINTER   @p filter is NULL.
 * @retval ARNM_ERROR_INVALID_PARAM  @p uuid is all zero, which names no community.
 * @whisper Which coin the balance was counted in
 */
arnm_result grdb_transactions_filter_set_coin_community(
    grdb_transactions_filter *filter, const uint8_t *uuid
);

/**
 * @brief Look only inside `[min_tx_nr, max_tx_nr]`, either end 0 for no bound.
 *
 * A @p min_tx_nr above @p max_tx_nr is allowed and matches nothing -- a caller narrowing a span
 * step by step may pass through that, and it is an answer, not a mistake.
 *
 * @param[in,out] filter     Filter to narrow; not NULL.
 * @param[in]     min_tx_nr  Smallest number to match, 0 for no bound.
 * @param[in]     max_tx_nr  Largest number to match, 0 for no bound.
 * @retval ARNM_SUCCESS            Set.
 * @retval ARNM_ERROR_NULL_POINTER @p filter is NULL.
 */
arnm_result grdb_transactions_filter_set_tx_range(
    grdb_transactions_filter *filter, uint64_t min_tx_nr, uint64_t max_tx_nr
);

/**
 * @brief Look only at transactions confirmed inside `[from_seconds, to_seconds]`, either end 0
 *        for no bound, the last second included.
 *
 * The index answers whole days, so a span is widened to the days it touches before it narrows
 * anything -- seconds inside a day cannot be told apart from the numbers alone.
 *
 * @param[in,out] filter       Filter to narrow; not NULL.
 * @param[in]     from_seconds Earliest second to match, 0 for no bound.
 * @param[in]     to_seconds   Latest second to match, 0 for no bound.
 * @retval ARNM_SUCCESS            Set.
 * @retval ARNM_ERROR_NULL_POINTER @p filter is NULL.
 * @whisper The days the question is asked of
 */
arnm_result grdb_transactions_filter_set_date_range(
    grdb_transactions_filter *filter, int64_t from_seconds, int64_t to_seconds
);

// ********** reading one back *******************

/**
 * @brief The address the filter looks for, or NULL when it looks for none.
 * @param[in] filter Filter to read; may be NULL.
 * @return 32 bytes inside @p filter -- they live as long as it does -- or NULL.
 */
const uint8_t *grdb_transactions_filter_address(const grdb_transactions_filter *filter);

/**
 * @brief The coin community the filter looks for, or NULL when it looks for any.
 * @param[in] filter Filter to read; may be NULL.
 * @return 16 bytes inside @p filter, or NULL.
 */
const uint8_t *grdb_transactions_filter_coin_community(const grdb_transactions_filter *filter);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_TRANSACTIONS_FILTER_H
