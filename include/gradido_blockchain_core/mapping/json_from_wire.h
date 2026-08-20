#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_FROM_WIRE_H
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_FROM_WIRE_H

#include "gradido_blockchain_core/mapping/json_format.h"
#include "gradido_blockchain_core/result.h"
#include "hostmem/memory_block.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grdm_json_from_wire grdm_json_from_wire
 *  @ingroup mapping
 *  @brief Rendering wire structures as JSON text, without touching malloc
 *
 *  The wire structures are what comes off the network, and this renders them as they stand:
 *  nested the way protobuf nests them, one function per message, each member under the name its
 *  field carries in the struct. A confirmed transaction holds a gradido transaction, which
 *  holds a signature map and the body as bytes -- and that is exactly what the text shows.
 *
 *  This is the difference to @ref grdm_json_from_runtime, which renders the same transaction
 *  flattened: there a transfer's sender is three members beside each other, here it is the
 *  `grdw_transfer_amount` object the wire actually carries. Use this view to see what arrived;
 *  use the runtime view to see what it means. Where the two describe the same thing they spell
 *  it the same way -- an amount is a decimal string in both, a ledger anchor has the same two
 *  members in both -- because both draw those from one place.
 *
 *  On the wire the body is a byte string inside the gradido transaction, and the two useful
 *  things to say about it are its bytes and its meaning. Those are two functions rather than a
 *  flag: reading the bytes cannot fail and needs nothing, while decoding them is a real step
 *  that can fail and needs a workspace the caller sizes. A signature that carried a size nobody
 *  uses half the time would hide that difference. The bytes are what the signatures are over,
 *  so a reader verifying one wants `..._to_json()`; a reader who wants to know what was signed
 *  wants `..._with_body_to_json()`. The member name says which it got -- `body_bytes` for the
 *  one, `body` for the other.
 *
 *  ### The text forms
 *
 *  - **Binary** -- keys, hashes, signatures, memo payloads, body bytes -- is lowercase hex, two
 *    characters per byte, no separator and no prefix.
 *  - **Community uuids** are the canonical 8-4-4-4-12 form.
 *  - **Timestamps** are `seconds.nanoseconds`, the nanoseconds always nine digits. Fields
 *    carrying whole seconds -- a creation's target date -- stay JSON numbers.
 *  - **Amounts** are decimal strings with four fractional digits, the form
 *    grdd_unit_from_string() reads back, never JSON numbers.
 *  - **Enums** are their enumerator names, from the same `grdt_*_to_string()` the rest of the
 *    project prints them with.
 *
 *  A member the structure does not carry is left out rather than written as null, so what is
 *  present is what the counts and the union made reachable. Arrays are the exception and are
 *  always written, empty ones included.
 *
 *  @whisper What arrived on the wire, said out loud in the shape it arrived in
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
 * @brief Render a wire transaction body as JSON text.
 *
 * The object carries the transaction type, the cross group type, the creation timestamp, the
 * memo array and -- under the name of the union member the type selects -- the payload:
 * `transfer`, `creation`, `community_friends_update`, `register_address`, `deferred_transfer`,
 * `community_root`, `redeem_deferred_transfer` or `timeout_deferred_transfer`. The optional
 * `other_community_uuid` appears only on a body that carries one.
 *
 * All eight payloads are described here, including the community friends update that
 * grdm_complete_transaction_from_wire() has no runtime shape for -- a wire body is rendered as
 * it is, not as far as the runtime can follow it.
 *
 * @param[out]    json   Receives the text; see the memory note below. Untouched on failure.
 * @param[in]     body   Body to render; not NULL. Read only, and not read past what its memo
 *                       count and its transaction type make reachable.
 * @param[in]     format Compact or indented; see @ref grdm_json_format.
 * @param[in,out] work   Chain for the rendering itself; not NULL.
 * @param[in,out] result Chain the finished text is placed in; not NULL.
 * @retval HOSTMEM_SUCCESS              @p json holds the text.
 * @retval HOSTMEM_ERROR_NULL_POINTER   An argument is NULL.
 * @retval HOSTMEM_ERROR_ENUM_UNHANDLED @p body carries GRDT_TRANSACTION_NONE or a value outside
 *                                      the enum, neither of which names a payload.
 * @retval HOSTMEM_ERROR_ENCODE_FAILED  A field could not be written as text: a timestamp whose
 *                                      nanoseconds fall outside 0..999999999, or an amount that
 *                                      does not round to four digits.
 * @retval HOSTMEM_ERROR_ARITHMETIC_OVERFLOW A field is larger than the @c uint32_t hostmem
 *                                      measures an allocation in.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY  A chain could not open another arena.
 * @whisper Intent, before anyone confirmed it
 */
hostmem_result grdm_transaction_body_to_json(
    hostmem_memory_block *json,
    const grdw_transaction_body *body,
    grdm_json_format format,
    hostmem_multi_arena *work,
    hostmem_multi_arena *result
);

/**
 * @brief Render a wire gradido transaction as JSON text, the body left as bytes.
 *
 * The object carries the signature map and, when they are there, the serialized body as hex
 * under `body_bytes` and the pairing ledger anchor.
 *
 * @param[out]    json   Receives the text; see the memory note below. Untouched on failure.
 * @param[in]     tx     Transaction to render; not NULL.
 * @param[in]     format Compact or indented; see @ref grdm_json_format.
 * @param[in,out] work   Chain for the rendering itself; not NULL.
 * @param[in,out] result Chain the finished text is placed in; not NULL.
 * @retval HOSTMEM_SUCCESS             @p json holds the text.
 * @retval HOSTMEM_ERROR_NULL_POINTER  An argument is NULL.
 * @retval HOSTMEM_ERROR_ENCODE_FAILED A timestamp in the pairing anchor carries nanoseconds
 *                                     outside 0..999999999.
 * @retval HOSTMEM_ERROR_ARITHMETIC_OVERFLOW The body bytes are larger than the @c uint32_t
 *                                     hostmem measures an allocation in.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY A chain could not open another arena.
 * @whisper Signatures around a payload no one here opens
 */
hostmem_result grdm_gradido_transaction_to_json(
    hostmem_memory_block *json,
    const grdw_gradido_transaction *tx,
    grdm_json_format format,
    hostmem_multi_arena *work,
    hostmem_multi_arena *result
);

/**
 * @brief The same, with the body decoded and described under `body`.
 *
 * The decode needs a stretch for pbtools to work in, and it is taken from @p work -- the same
 * chain the rendering itself draws from, so a caller opens no third allocator. Whatever the
 * decode builds goes there too and leaves with the caller's reset.
 *
 * @p pb_workspace_size is the caller's estimate, and deliberately so: a stretch too small comes
 * back as HOSTMEM_ERROR_OUT_OF_MEMORY with nothing else wrong, so a caller reading a stream
 * raises the figure once, on the first body that does not fit, and passes the same larger one to
 * every call after it. A guess made inside would be wrong on the same body every time. Something
 * in the order of eight times the body's length plus a kilobyte is a workable starting point;
 * measure your own messages rather than trusting that.
 *
 * @param[out]    json              Receives the text; see the memory note below. Untouched on
 *                                  failure.
 * @param[in]     tx                Transaction to render; not NULL.
 * @param[in]     format            Compact or indented; see @ref grdm_json_format.
 * @param[in]     pb_workspace_size Bytes to reserve from @p work for pbtools; must be > 0.
 * @param[in,out] work              Chain for the rendering and the decode; not NULL.
 * @param[in,out] result            Chain the finished text is placed in; not NULL.
 * @retval HOSTMEM_SUCCESS              @p json holds the text.
 * @retval HOSTMEM_ERROR_NULL_POINTER   An argument is NULL.
 * @retval HOSTMEM_ERROR_INVALID_PARAM  @p pb_workspace_size is 0.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY  The stretch was too small for the body, or a chain could
 *                                      not open another arena. Enlarge and call again.
 * @retval HOSTMEM_ERROR_DECODE_FAILED  The body bytes are not a transaction body.
 * @retval HOSTMEM_ERROR_ENUM_UNHANDLED The decoded body carries GRDT_TRANSACTION_NONE or a value
 *                                      outside the enum.
 * @retval HOSTMEM_ERROR_ENCODE_FAILED  A field could not be written as text.
 * @whisper The seal is broken, and the letter read out
 */
hostmem_result grdm_gradido_transaction_with_body_to_json(
    hostmem_memory_block *json,
    const grdw_gradido_transaction *tx,
    grdm_json_format format,
    uint32_t pb_workspace_size,
    hostmem_multi_arena *work,
    hostmem_multi_arena *result
);

/**
 * @brief Render a wire confirmed transaction as JSON text, the body left as bytes.
 *
 * The object carries the transaction number, the nested gradido transaction under `transaction`,
 * the confirmation timestamp, the running hash, the ledger anchor, the balance derivation and
 * the account balances after the transaction applied.
 *
 * @param[out]    json   Receives the text; see the memory note below. Untouched on failure.
 * @param[in]     tx     Transaction to render; not NULL. Read only, and not read past what its
 *                       balance count makes reachable.
 * @param[in]     format Compact or indented; see @ref grdm_json_format.
 * @param[in,out] work   Chain for the rendering itself; not NULL.
 * @param[in,out] result Chain the finished text is placed in; not NULL.
 * @retval HOSTMEM_SUCCESS             @p json holds the text.
 * @retval HOSTMEM_ERROR_NULL_POINTER  An argument is NULL.
 * @retval HOSTMEM_ERROR_ENCODE_FAILED A timestamp carries nanoseconds outside 0..999999999, or
 *                                     a balance does not round to four digits.
 * @retval HOSTMEM_ERROR_ARITHMETIC_OVERFLOW A field is larger than the @c uint32_t hostmem
 *                                     measures an allocation in.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY A chain could not open another arena.
 * @whisper The ledger's own account of what settled
 */
hostmem_result grdm_confirmed_transaction_to_json(
    hostmem_memory_block *json,
    const grdw_confirmed_transaction *tx,
    grdm_json_format format,
    hostmem_multi_arena *work,
    hostmem_multi_arena *result
);

/**
 * @brief The same, with the nested transaction's body decoded and described under `body`.
 *
 * @p pb_workspace_size reaches the nested gradido transaction, which is where the body lives,
 * and means there exactly what it means in
 * grdm_gradido_transaction_with_body_to_json() -- read the note on sizing there.
 *
 * @param[out]    json              Receives the text; see the memory note below. Untouched on
 *                                  failure.
 * @param[in]     tx                Transaction to render; not NULL.
 * @param[in]     format            Compact or indented; see @ref grdm_json_format.
 * @param[in]     pb_workspace_size Bytes to reserve from @p work for pbtools; must be > 0.
 * @param[in,out] work              Chain for the rendering and the decode; not NULL.
 * @param[in,out] result            Chain the finished text is placed in; not NULL.
 * @retval Everything grdm_gradido_transaction_with_body_to_json() reports, for the same reasons.
 * @whisper The ledger's account, and the letter it settled about
 */
hostmem_result grdm_confirmed_transaction_with_body_to_json(
    hostmem_memory_block *json,
    const grdw_confirmed_transaction *tx,
    grdm_json_format format,
    uint32_t pb_workspace_size,
    hostmem_multi_arena *work,
    hostmem_multi_arena *result
);

/**
 * @page grdm_json_from_wire_memory The two chains
 *
 * Every function above takes the same pair of allocators, and the split between them is the
 * point: @p work carries everything the rendering needs and nothing that outlives it -- the
 * mutable document, the hex and decimal strings the fields are turned into, the writer's own
 * output buffer -- while @p result receives one allocation, the finished text and nothing else.
 * Neither these functions nor yyjson under them ever name malloc.
 *
 * Where the chains themselves draw from is the caller's to decide. A chain reaches malloc only
 * when it has to open an arena, so a chain whose arenas are already there does not: one fed
 * caller owned buffers through hostmem_multi_arena_borrow(), with a bookkeeping allocator of
 * its own, never asks the host for anything; and a chain that has served one structure and been
 * reset serves the next from the same arenas.
 *
 * @p work fills and empties with every call and is left holding the scratch of it, on success
 * and on failure alike -- a bump chain gives nothing back one block at a time, so
 * hostmem_multi_arena_reset() is what empties it. @p result grows one structure at a time and
 * holds what the caller still wants to read.
 *
 * In the @c json block, @c data points into @p result and @c size counts the characters. One
 * further byte is written after them, a terminator, so the block is a C string as well -- which
 * means reclaiming it by hand through hostmem_multi_arena_free() passes @c size + 1.
 *
 * @note Not thread safe in the allocators: one chain used from two calls at once is a data
 *       race, the same as anywhere else in hostmem.
 */

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_FROM_WIRE_H
