#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_RUNTIME_FROM_JSON_H
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_RUNTIME_FROM_JSON_H

#include "arnm/json_reader.h"
#include "arnm/memory.h"
#include "gradido_blockchain_core/result.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grdm_runtime_from_json grdm_runtime_from_json
 *  @ingroup mapping
 *  @brief Conversion from JSON text back to runtime structures
 *
 *  The way back from @ref grdm_json_from_runtime, and its exact inverse: the document that
 *  mapping wrote reads here into the transaction it came from, field for field. Nothing is
 *  inferred and nothing is recomputed -- what the text carries is what the transaction gets,
 *  which is what makes a round trip a copy rather than a reconstruction.
 *
 *  The document's shape, the hex for binary and the enumerator names for enumerations are all
 *  documented once, at @ref grdm_json_from_runtime. Read that for what a document looks like;
 *  read this for what happens to one that is not quite right.
 *
 *  ### What a document may leave out
 *
 *  A member that is absent is a member left at zero -- the same state
 *  @c grdr_complete_transaction_init() leaves. That covers the fields a transaction type does
 *  not own (a transfer has no @c target_date) and the two cross-group members, which are only
 *  ever there on a transaction that is not local. Everything a type does own is required, and
 *  a missing one is refused rather than defaulted, because a silent zero in a public key or an
 *  amount is the expensive kind of forgiveness.
 *
 *  ### Where the memory comes from
 *
 *  Two allocators, and they are not the same one. @p allocator carries the parsed document and
 *  is finished with before the call returns. The arrays and byte blocks the transaction keeps
 *  are drawn into an arena of its own, sized in one pass over the document before a byte of it
 *  is copied, and released by @c grdr_complete_transaction_release() -- exactly as
 *  @ref grdm_complete_transaction_from_wire does it.
 *  @{
 */

// forward declaration from gradido data runtime
typedef struct grdr_complete_transaction grdr_complete_transaction;

/**
 * @brief Read a complete runtime transaction back out of one JSON object.
 *
 * @p tx is released first, so a transaction that already carries an arena is reused rather
 * than leaked; on any failure it is released again and left as
 * @c grdr_complete_transaction_init() leaves it, holding nothing.
 *
 * The reading runs to the end before it is judged. arnm's reader keeps the first refusal and
 * the field it happened at and answers empty for everything after, so a document missing a
 * member in the middle is not read half way and abandoned -- it is read through, and asked
 * about once. What comes back is that first refusal.
 *
 * @param[out]    tx          Transaction to fill; not NULL. Every field is written and none is
 *                            read, so uninitialised storage is a valid input.
 * @param[in]     json        The document; not NULL. Left untouched -- the parse takes a copy.
 * @param[in]     json_length Bytes in @p json, terminator excluded; must be > 0 and at most
 *                            @ref ARNM_JSON_READER_MAX_INPUT_SIZE.
 * @param[in,out] allocator   Where the parsed document comes from, or NULL for the host. It
 *                            carries nothing once the call returns; the transaction's own
 *                            arena is opened from the host either way.
 * @param[in]     flags       Bit set of @c ARNM_JSON_READ_* , or @ref ARNM_JSON_READ_DEFAULT
 *                            for strict RFC 8259.
 * @retval ARNM_SUCCESS                    @p tx holds the transaction the document described.
 * @retval ARNM_ERROR_NULL_POINTER         @p tx or @p json is NULL.
 * @retval ARNM_ERROR_INVALID_PARAM        @p json_length is 0, or @p flags holds a bit arnm
 *                                         does not define.
 * @retval ARNM_ERROR_DECODE_FAILED        The text is not JSON, or a hex string is not an even
 *                                         number of hex digits, or one standing for a
 *                                         fixed-size field is not exactly twice its length, or
 *                                         a uuid is not the canonical 36 characters.
 * @retval ARNM_ERROR_INVALID_ENUM_TYPE    A member is there but is of another JSON type than
 *                                         the field it names -- a number where a string
 *                                         belongs, an object where an array belongs.
 * @retval ARNM_ERROR_ENUM_UNKNOWN         An enumeration member spells no enumerator this
 *                                         library has.
 * @retval ARNM_ERROR_ENUM_UNHANDLED       @c transaction_type names a transaction this mapping
 *                                         has no layout for -- the same refusal
 *                                         @ref grdm_complete_transaction_from_wire answers.
 * @retval ARNM_ERROR_ARITHMETIC_OVERFLOW  A number does not fit the field it names.
 * @retval ARNM_ERROR_RESOURCE_SIZE_EXCEED What the arrays and blocks add up to is past
 *                                         @ref ARNM_MAX_ALLOC_SIZE, so no arena could hold
 *                                         them.
 * @retval ARNM_ERROR_OUT_OF_MEMORY        @p allocator, or the host the arena comes from, had
 *                                         nothing left.
 *
 * @see grdm_json_from_complete_transaction
 * @whisper Words settle back into bytes, and the ledger remembers what it always was
 */
arnm_result grdm_complete_transaction_from_json(
    grdr_complete_transaction *tx,
    const char *json,
    uint32_t json_length,
    arnm *allocator,
    arnm_json_read_flags flags
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_RUNTIME_FROM_JSON_H
