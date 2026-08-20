#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_FROM_RUNTIME_H
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_FROM_RUNTIME_H

#include "gradido_blockchain_core/result.h"
#include "hostmem/memory_block.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grdm_json_from_runtime grdm_json_from_runtime
 *  @ingroup mapping
 *  @brief Rendering runtime structures as JSON text, without touching malloc
 *
 *  The sibling mappers move between wire, protobuf and runtime -- shapes that all carry the
 *  same bytes. This one leaves that circle: a runtime transaction goes out as text, and text
 *  is where the binary fields have to be given a readable form. The rules are fixed, so that a
 *  reader and a writer never disagree about what a field means:
 *
 *  - **Keys, hashes, signatures and memo payloads** are lowercase hex, two characters per byte,
 *    no separator and no prefix.
 *  - **Community uuids** are the canonical 8-4-4-4-12 form.
 *  - **Timestamps** are `seconds.nanoseconds`, the nanoseconds always nine digits, as
 *    grdd_timestamp_to_string() writes them. Fields that carry whole seconds only -- a target
 *    date, a timeout -- stay JSON numbers.
 *  - **Amounts and balances** are decimal strings with four fractional digits, the form
 *    grdd_unit_from_string() reads back. They are never JSON numbers: a fixed-point value of
 *    scale 10^4 passed through a double comes back changed.
 *  - **Enums** are their enumerator names (`"GRDT_TRANSACTION_CREATION"`), from the same
 *    `grdt_*_to_string()` the rest of the project prints them with.
 *
 *  A field the transaction does not carry is left out rather than written as null, so the
 *  members present in the object are exactly the members the union and the counts made
 *  reachable. The one exception is named at
 *  grdm_complete_transaction_to_json().
 *
 *  @whisper The same transaction, told in a language people read
 *  @{
 */

// forward declarations from gradido data runtime
typedef struct grdr_complete_transaction grdr_complete_transaction;

// forward declaration from hostmem; the definition lives in "hostmem/multi_arena.h" and is
// needed only by callers that open a chain, not by this header, which passes it on by pointer
typedef struct hostmem_multi_arena hostmem_multi_arena;

/**
 * @brief Shape of the rendered text.
 *
 * @whisper The same words, set either close together or spread out
 */
typedef enum grdm_json_format {
  //! No whitespace between tokens -- the form to store or to send.
  GRDM_JSON_COMPACT = 0,
  //! Four space indent, one member per line -- the form to read.
  GRDM_JSON_PRETTY
} grdm_json_format;

/**
 * @brief Render a complete runtime transaction as JSON text.
 *
 * Two allocators, and the split between them is the point of this function: @p work carries
 * everything the rendering needs and nothing that outlives it -- the mutable document, the
 * hex and decimal strings the fields are turned into, the writer's own output buffer -- while
 * @p result receives one allocation, the finished text and nothing else. Neither this
 * function nor yyjson under it ever names malloc: every byte is drawn from the two chains,
 * through the allocator interface yyjson is handed.
 *
 * Where the chains themselves draw from is then the caller's to decide, and that is where a
 * run becomes free of the host entirely. A chain reaches malloc only when it has to open an
 * arena, so a chain whose arenas are already there does not: one fed caller owned buffers
 * through hostmem_multi_arena_borrow(), with a bookkeeping allocator of its own, never asks
 * the host for anything; and a chain that has served one transaction and been reset serves
 * the next one from the same arenas. The first transaction through a fresh chain is the one
 * that pays.
 *
 * The two chains are used differently and want to be reset differently. @p work fills and
 * empties with every call: reset it right after, and the next transaction runs through arenas
 * that are already open, asking the host for nothing. @p result grows one transaction at a
 * time and holds what the caller still wants to read, so it is the caller's to reset once the
 * text has been consumed.
 *
 * @param[out]    json   Receives the text: @c data points into @p result, @c size counts the
 *                       characters. One further byte is written after them, a terminator, so
 *                       the block is a C string as well -- which means reclaiming it by hand
 *                       through hostmem_multi_arena_free() passes @c size + 1. Untouched on
 *                       failure.
 * @param[in]     tx     Transaction to render; not NULL. Read only, and not read past what its
 *                       counts and its transaction type make reachable.
 * @param[in]     format Compact or indented; see @ref grdm_json_format.
 * @param[in,out] work   Chain for the rendering itself; not NULL. Left holding the scratch of
 *                       this call, on success and on failure alike -- a bump chain gives
 *                       nothing back one block at a time, so hostmem_multi_arena_reset() is
 *                       what empties it.
 * @param[in,out] result Chain the finished text is placed in; not NULL. May be the same chain
 *                       as @p work, at the price of the scratch staying beside the text until
 *                       the reset.
 * @retval HOSTMEM_SUCCESS                   @p json holds the text.
 * @retval HOSTMEM_ERROR_NULL_POINTER        An argument is NULL.
 * @retval HOSTMEM_ERROR_ENUM_UNHANDLED      @p tx carries a transaction type this mapping does
 *                                           not describe -- the same types
 *                                           grdm_complete_transaction_from_wire() refuses to
 *                                           build, so a transaction that came through it can
 *                                           always be rendered.
 * @retval HOSTMEM_ERROR_ENCODE_FAILED       A field could not be written as text: a timestamp
 *                                           whose nanoseconds fall outside 0..999999999, or an
 *                                           amount that does not round to four digits.
 * @retval HOSTMEM_ERROR_ARITHMETIC_OVERFLOW The text is longer than the @c uint32_t hostmem
 *                                           measures an allocation in.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY       A chain could not open another arena.
 *
 * @note The all-zero @c sender_pubkey a creation transaction carries is written as @c null
 *       rather than as sixty-four zeros: the struct documents those bytes as the absence of a
 *       sender, and the text says the same thing.
 * @note Not thread safe in the allocators: one chain used from two calls at once is a data
 *       race, the same as anywhere else in hostmem.
 * @whisper Every field finds the shape a reader knows it by, and the stream carries them out
 */
hostmem_result grdm_complete_transaction_to_json(
    hostmem_memory_block *json,
    const grdr_complete_transaction *tx,
    grdm_json_format format,
    hostmem_multi_arena *work,
    hostmem_multi_arena *result
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_FROM_RUNTIME_H
