#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_WIRE_FROM_JSON_H
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_WIRE_FROM_JSON_H

#include "gradido_blockchain_core/result.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grdm_wire_from_json grdm_wire_from_json
 *  @ingroup mapping
 *  @brief Reading wire structures back out of the JSON @ref grdm_json_from_wire writes
 *
 *  The way back. What @ref grdm_json_from_wire renders, this reads, and a structure that makes
 *  the round trip comes back equal to the one that set out -- which is the property the tests
 *  hold it to and the only useful definition of "the way back".
 *
 *  Everything the writing side fixed, the reading side requires: hex is lowercase and of exactly
 *  the length the field takes, uuids are the canonical 8-4-4-4-12 form, timestamps are
 *  `seconds.nanoseconds` with the nanoseconds always nine digits, amounts are decimal strings,
 *  and enums are their enumerator names. The enum names are checked against the same
 *  `grdt_*_to_string()` the writing side prints them with rather than against a second table, so
 *  the two cannot drift apart.
 *
 *  A member that is absent is absent, not empty: the arrays and the optional members are exactly
 *  the ones the writing side leaves out when there is nothing to say. A member that is present
 *  and wrong is an error, never a default.
 *
 *  ### What does not come back
 *
 *  grdm_gradido_transaction_with_body_to_json() renders the body as the object it decodes to,
 *  and that object cannot be read back into a gradido transaction here. The wire structure keeps
 *  the body as the exact bytes the signatures are over, and re-encoding an object is not a way
 *  to recover them -- protobuf leaves enough freedom that the bytes that come out need not be
 *  the bytes that went in, and a signature does not survive "need not". So a transaction whose
 *  JSON carries `body` and no `body_bytes` is refused, naming `body_bytes` as what it wanted.
 *
 *  A caller who does want the structure of such a body calls
 *  grdm_transaction_body_from_json() on the `body` member itself and decides for itself what
 *  the result is good for.
 *
 *  @whisper Text settles back into the shape it was poured from
 *  @{
 */

// forward declarations from gradido data wire
typedef struct grdw_confirmed_transaction grdw_confirmed_transaction;
typedef struct grdw_gradido_transaction grdw_gradido_transaction;
typedef struct grdw_transaction_body grdw_transaction_body;

// forward declaration from hostmem; the definition lives in "hostmem/multi_arena.h" and is
// needed only by callers that open a chain, not by this header, which passes it on by pointer
typedef struct hostmem_multi_arena hostmem_multi_arena;

/**
 * @brief Where a read went wrong, for a caller that has to tell someone.
 *
 * Both fields are string literals from this library's own text and are never allocated, so a
 * caller may keep them for as long as it likes and a failing read costs no memory. That is the
 * whole reason this is not @ref grd_error_details: a reader of untrusted JSON fails often, and
 * failing must not allocate.
 *
 * @whisper The place the path gave way, named plainly
 */
typedef struct grdm_json_error {
  /**
   * @brief Dotted path of the member at fault, e.g. `"transfer.sender.pubkey"`, or NULL.
   *
   * An index in a path is written as the member name alone: the arrays are read in order and
   * the element that failed is the last one begun. NULL when the failure is not about a member
   * -- the text is not JSON at all, or an allocator ran dry.
   */
  const char *member;
  //! One sentence on what was wrong there, or NULL. Never a copy of the input.
  const char *reason;
} grdm_json_error;

/**
 * @brief Read a transaction body out of the JSON grdm_transaction_body_to_json() writes.
 *
 * @param[out]    body      Body to fill; not NULL. Initialised first, so a previously filled one
 *                          is valid input, and left initialised-and-empty on failure.
 * @param[in]     json      The text; not NULL. Not modified.
 * @param[in]     json_size Its length in bytes, terminator not counted; must be > 0.
 * @param[out]    error     Where the read went wrong; may be NULL. Untouched on success.
 * @param[in,out] work      Chain the parsed document is built in; not NULL. Holds it on return,
 *                          on success and on failure alike, and is the caller's to reset.
 * @param[in,out] allocator Chain the body's own memory comes from -- its memo array and the memo
 *                          payloads; not NULL. Outlives the call, and is what
 *                          grdw_transaction_body_free() gives back.
 * @retval HOSTMEM_SUCCESS             @p body is filled.
 * @retval HOSTMEM_ERROR_NULL_POINTER  An argument is NULL.
 * @retval HOSTMEM_ERROR_INVALID_PARAM @p json_size is 0.
 * @retval HOSTMEM_ERROR_DECODE_FAILED The text is not JSON, or a member is missing, of the wrong
 *                                     type, or holds something the field cannot take. @p error
 *                                     names which.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY A chain could not open another arena.
 * @whisper Intent, read back out of the words for it
 */
hostmem_result grdm_transaction_body_from_json(
    grdw_transaction_body *body,
    const char *json,
    size_t json_size,
    grdm_json_error *error,
    hostmem_multi_arena *work,
    hostmem_multi_arena *allocator
);

/**
 * @brief Read a gradido transaction out of the JSON grdm_gradido_transaction_to_json() writes.
 *
 * The body must be present as `body_bytes` if it is present at all -- see the module text for
 * why the decoded `body` is not accepted here.
 *
 * @param[out]    tx        Transaction to fill; not NULL. Initialised first.
 * @param[in]     json      The text; not NULL. Not modified.
 * @param[in]     json_size Its length in bytes; must be > 0.
 * @param[out]    error     Where the read went wrong; may be NULL.
 * @param[in,out] work      Chain the parsed document is built in; not NULL.
 * @param[in,out] allocator Chain the transaction's own memory comes from -- its signature array
 *                          and body bytes; not NULL.
 * @retval Everything grdm_transaction_body_from_json() reports, for the same reasons.
 * @whisper Signatures and a sealed payload, gathered back up
 */
hostmem_result grdm_gradido_transaction_from_json(
    grdw_gradido_transaction *tx,
    const char *json,
    size_t json_size,
    grdm_json_error *error,
    hostmem_multi_arena *work,
    hostmem_multi_arena *allocator
);

/**
 * @brief Read a confirmed transaction out of the JSON
 *        grdm_confirmed_transaction_to_json() writes.
 *
 * @param[out]    tx        Transaction to fill; not NULL. Initialised first.
 * @param[in]     json      The text; not NULL. Not modified.
 * @param[in]     json_size Its length in bytes; must be > 0.
 * @param[out]    error     Where the read went wrong; may be NULL.
 * @param[in,out] work      Chain the parsed document is built in; not NULL.
 * @param[in,out] allocator Chain the transaction's own memory comes from -- its balance array
 *                          and everything the nested gradido transaction keeps; not NULL.
 * @retval Everything grdm_transaction_body_from_json() reports, for the same reasons.
 * @whisper The ledger's account, taken back off the page
 */
hostmem_result grdm_confirmed_transaction_from_json(
    grdw_confirmed_transaction *tx,
    const char *json,
    size_t json_size,
    grdm_json_error *error,
    hostmem_multi_arena *work,
    hostmem_multi_arena *allocator
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_WIRE_FROM_JSON_H
