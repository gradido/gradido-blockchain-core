#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_FROM_RUNTIME_H
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_FROM_RUNTIME_H

#include "arnm/json_writer.h"
#include "arnm/memory.h"
#include "arnm/memory_block.h"
#include "gradido_blockchain_core/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grdm_json_from_runtime grdm_json_from_runtime
 *  @ingroup mapping
 *  @brief Conversion from runtime structures to JSON text
 *
 *  The readable bank of the runtime transaction: what the wire keeps in packed bytes is set
 *  down here as named fields a person can read and a foreign runtime can parse.
 *  @ref grdm_runtime_from_json is the way back, and the two are exact inverses -- every field
 *  written here is read there, so a transaction survives the round trip unchanged, bit for
 *  bit, including the fields the type in hand does not use.
 *
 *  ### Where binary becomes text
 *
 *  Public keys, hashes, signatures, the encrypted memos and `body_bytes` are written as
 *  lowercase hex, two characters per byte and no separators -- @c arnm_binary_to_hex(). Hex
 *  rather than base64, because base64 in this project needs libsodium and the mapping has to
 *  hold in a build without it. Community uuids take the canonical 8-4-4-4-12 form instead,
 *  which is how a uuid is read everywhere else.
 *
 *  Enumerations are written as their enumerator's own spelling --
 *  @c "GRDT_TRANSACTION_TRANSFER", not @c 2. A number would tie the document to the order the
 *  enum happens to have today; the name stays right when a value is inserted.
 *
 *  ### The shape of a document
 *
 *  One object. `tx_nr`, the two timestamps, the community uuid, the ledger anchor, the three
 *  type fields and the running hash are always there. Then the detail of the transaction under
 *  the one member its type owns -- @c "transfer", @c "register_address" or
 *  @c "community_root" -- and its context beside it, @c "target_date",
 *  @c "timeout_duration" or @c "previous_tx". The three arrays follow, each written even when
 *  empty. @c "tx_pairing_community_uuid" and @c "pairing_ledger_anchor" appear only on a
 *  transaction that is not local, exactly as they are only set there.
 *
 *  @code
 *  arnm scratch;
 *  arnm_init_arena(&scratch, 8192);
 *
 *  arnm_memory_block text;
 *  if (ARNM_SUCCESS == grdm_json_from_complete_transaction(
 *          &text, &tx, &scratch, ARNM_JSON_WRITE_PRETTY)) {
 *    puts((const char *)text.data);
 *    arnm_memory_block_free(&text, &scratch);
 *  }
 *  arnm_release(&scratch);
 *  @endcode
 *  @{
 */

// forward declaration from gradido data runtime
typedef struct grdr_complete_transaction grdr_complete_transaction;

/**
 * @brief Write a complete runtime transaction as one JSON object.
 *
 * Every field of @p tx is set down, the document is rendered under @p flags, and what comes
 * back is a NUL terminated block the caller owns. Nothing of @p tx is read after the call
 * returns: the text is a copy and no longer shares a byte with the transaction it came from.
 *
 * @param[out]    out       Receives the rendered text: @c data NUL terminated, @c size the
 *                          bytes reserved for it. Not NULL, and untouched unless the call
 *                          succeeds. Give it back with @c arnm_memory_block_free() against
 *                          @p allocator.
 * @param[in]     tx        Transaction to write; not NULL. Read only.
 * @param[in,out] allocator Where the document being built and the finished text come from, or
 *                          NULL for the host. An arena is the natural choice -- the whole
 *                          write is a handful of allocations that are wanted only until the
 *                          text has been sent on.
 * @param[in]     flags     Bit set of @c ARNM_JSON_WRITE_*, or @ref ARNM_JSON_WRITE_DEFAULT
 *                          for the minified form. @ref ARNM_JSON_WRITE_PRETTY is the one to
 *                          reach for when a person will read the result.
 * @retval ARNM_SUCCESS                   The text is in @p out.
 * @retval ARNM_WARNING_ARENA_MEMORY_NOT_RECLAIMED The text is in @p out and is complete; an
 *                                        arena kept scratch that only its reset will take
 *                                        back. Neither a success to celebrate nor a failure to
 *                                        unwind -- the document is good, the memory is not
 *                                        back yet.
 * @retval ARNM_ERROR_NULL_POINTER        @p out or @p tx is NULL.
 * @retval ARNM_ERROR_INVALID_PARAM       @p flags holds a bit arnm does not define.
 * @retval ARNM_ERROR_ENUM_UNHANDLED      @c tx->transaction_type carries no transaction this
 *                                        mapping knows how to lay out -- the same refusal
 *                                        @ref grdm_complete_transaction_from_wire answers for
 *                                        the same types.
 * @retval ARNM_ERROR_RESOURCE_SIZE_EXCEED A memo or @c body_bytes is longer than half of
 *                                        @ref ARNM_MAX_ALLOC_SIZE, so its hex could not be
 *                                        counted without wrapping.
 * @retval ARNM_ERROR_OUT_OF_MEMORY       @p allocator had nothing left.
 *
 * @note Failure leaves @p out exactly as the caller had it, so uninitialised storage is a
 *       valid input.
 * @see grdm_complete_transaction_from_json
 * @whisper The ledger's packed bytes unfolded into words, and the words hold everything back
 */
arnm_result grdm_json_from_complete_transaction(
    arnm_memory_block *out,
    const grdr_complete_transaction *tx,
    arnm *allocator,
    arnm_json_write_flags flags
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_FROM_RUNTIME_H
