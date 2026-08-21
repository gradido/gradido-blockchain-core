#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_PB_WORKSPACE_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_PB_WORKSPACE_H

#include "hostmem/memory_block.h"
#include "hostmem/multi_arena.h"
#include "hostmem/result.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grdw_pb_workspace grdw_pb_workspace
 *  @ingroup wire
 *  @brief The workspace pbtools bumps through, and the judgement on what it reported
 *
 * Support for the three encoders and decoders in this folder rather than something to call from
 * outside them: nothing here means anything without the generated pbtools calls it surrounds.
 *
 * ### The workspace is the caller's
 *
 * pbtools is handed one stretch of memory and bumps through it; it cannot ask for more. Every
 * encode and decode in this folder therefore takes that stretch as a @ref hostmem_memory_block
 * the caller sized and owns, and reports HOSTMEM_ERROR_OUT_OF_MEMORY when it was not enough.
 *
 * That is deliberately the caller's decision and not a guess made in here. A caller knows what
 * it is reading -- a transaction from its own ledger, a message of a shape it has seen a
 * thousand times -- and a caller in a loop learns the size once, on the first message that does
 * not fit, and hands the larger stretch to every call after it. A guess made here would either
 * be too small on the same message every time or too large on all the others, and it would
 * repeat the decode to find out.
 *
 * On the way through, the stretch is written but never read back by this code and never kept:
 * pbtools works in it, the `grdm_*_from_pbtools()` step copies out what the wire structure
 * keeps, and the caller is free to reuse the same bytes for the next message. Nothing the call
 * hands back points into it, which is what makes that reuse safe. The one time a caller may
 * want to look at the bytes themselves is the failure the note below describes.
 *
 * @note The stretch must be 8 byte aligned, which is what every hostmem allocation already is.
 * @note **A failing decode does not reset the workspace.** Nothing here writes over what
 *       pbtools left, so the stretch comes back holding partial data rather than either its
 *       previous contents or a decoded message: the empty message `proto_*_new()` lays down
 *       before any byte is read, plus whatever fields the decode managed before it gave up.
 *       That is deliberate -- it is exactly what someone wants to look at when a message will
 *       not decode, and the stretch belongs to the caller anyway. The output structure was
 *       never filled from it, so nothing points in, and the next call may be handed the same
 *       bytes: each decode starts by laying its own message down over them.
 *
 * @whisper The ground is measured out by the one who knows the walk
 *  @{
 */

/**
 * @brief hostmem_memory_block_alloc() against a chain.
 *
 * hostmem ships the memory_block helpers for a single arena only, and the three messages here
 * keep pointer and size together the way those helpers do. Three lines each, rather than three
 * lines at every call site.
 *
 * @param[out]    block     Descriptor to fill; not NULL. Written only once the whole call has
 *                          succeeded, so a refusal leaves both its members exactly as they
 *                          were: a descriptor that already named an allocation still names it,
 *                          a zeroed one is still empty, and there is no half-filled state for a
 *                          caller to check for.
 * @param[in]     size      Bytes to reserve; must be > 0. The chain reserves the next multiple
 *                          of 8 for them. They are not zeroed -- they hold whatever the
 *                          previous tenant of that stretch left.
 * @param[in,out] allocator Chain to draw from; not NULL. NULL is refused rather than taken as a
 *                          malloc fallback.
 * @retval HOSTMEM_SUCCESS             @p block names @p size usable bytes.
 * @retval HOSTMEM_ERROR_NULL_POINTER  @p block or @p allocator is NULL.
 * @retval HOSTMEM_ERROR_INVALID_PARAM @p size is 0.
 * @retval Anything else hostmem_multi_arena_alloc() reports: no arena had room and none could
 *         be opened, or rounding @p size up to 8 would wrap uint32_t.
 */
static inline hostmem_result grdw_block_alloc(
    hostmem_memory_block *block, uint32_t size, hostmem_multi_arena *allocator
) {
  if (!block) { return HOSTMEM_ERROR_NULL_POINTER; }
  hostmem_result taken = hostmem_multi_arena_alloc(&block->data, size, allocator);
  if (HOSTMEM_SUCCESS != taken) { return taken; }
  block->size = size;
  return HOSTMEM_SUCCESS;
}

/**
 * @brief hostmem_memory_block_clone() against a chain.
 *
 * @param[out]    dst       Descriptor to fill; not NULL. On success it names a fresh copy taken
 *                          from @p allocator, never the source bytes. A refusal leaves it
 *                          untouched, the same as grdw_block_alloc() above.
 * @param[in]     src       Source to copy; not NULL. Its @c data must hold @c size bytes,
 *                          unless @c size is 0. Read only; the copy does not alias it.
 * @param[in,out] allocator Chain the copy is taken from; not NULL.
 * @retval HOSTMEM_SUCCESS            @p dst names the copy, or was emptied for an empty source.
 * @retval HOSTMEM_ERROR_NULL_POINTER @p dst, @p src, @p src->data or @p allocator is NULL.
 * @retval Anything else hostmem_multi_arena_alloc() reports, for the allocation the copy needs.
 *
 * @note An empty source does not leave @p dst alone -- it empties it to @c {NULL, 0} and reports
 *       success. A descriptor that named an allocation before such a call stops naming it: the
 *       bytes stay reserved in their arena until it is reset, with nothing pointing at them any
 *       more. Clone into a fresh descriptor, or hand the old one to grdw_block_free() first.
 */
static inline hostmem_result grdw_block_clone(
    hostmem_memory_block *dst, const hostmem_memory_block *src, hostmem_multi_arena *allocator
) {
  if (!dst || !src) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!src->size) {
    dst->data = NULL;
    dst->size = 0;
    return HOSTMEM_SUCCESS;
  }
  hostmem_result cloned = hostmem_multi_arena_clone(&dst->data, src->data, src->size, allocator);
  if (HOSTMEM_SUCCESS != cloned) { return cloned; }
  dst->size = src->size;
  return HOSTMEM_SUCCESS;
}

/**
 * @brief hostmem_memory_block_free() against a chain.
 *
 * Reclaims only while the block is the tail of its arena, the same bargain a single arena
 * offers. The descriptor is emptied either way, so a caller cannot read a block it has released.
 *
 * @param[in,out] block     Descriptor to release; may be NULL, which does nothing. It must be
 *                          one grdw_block_alloc() or grdw_block_clone() filled and that nothing
 *                          has edited since: the size it carries is what the arena is told to
 *                          take back, and a size that does not match the allocation moves the
 *                          bump index by the wrong amount and hands the same bytes out twice.
 *                          An already empty descriptor is left empty and nothing is reclaimed.
 * @param[in,out] allocator Chain the block was taken from. NULL, or a different chain than the
 *                          one it came from, reclaims nothing -- and the descriptor is emptied
 *                          all the same, so this cannot be detected afterwards.
 *
 * Returns nothing on purpose: whether the bytes came back is not something a caller can act on.
 * A block that was not the tail stays reserved until hostmem_multi_arena_reset(), which is how
 * memory really comes back from a bump chain, and the descriptor reads @c {NULL, 0} either way.
 */
static inline void grdw_block_free(hostmem_memory_block *block, hostmem_multi_arena *allocator) {
  if (!block) { return; }
  if (block->data) { (void)hostmem_multi_arena_free(block->data, block->size, allocator); }
  block->data = NULL;
  block->size = 0;
}

/**
 * @brief Check a caller supplied workspace before pbtools is handed it.
 *
 * @param[in] workspace Stretch to check; may be NULL, which is what this reports on.
 * @retval HOSTMEM_SUCCESS             Usable.
 * @retval HOSTMEM_ERROR_NULL_POINTER  @p workspace or its data pointer is NULL.
 * @retval HOSTMEM_ERROR_INVALID_PARAM It holds no bytes, or its start is not 8 byte aligned.
 */
hostmem_result grdw_pb_workspace_check(const hostmem_memory_block *workspace);

/**
 * @brief Translate what the generated decode reported.
 *
 * @param[in] decoded_bytes What the generated decode returned: bytes consumed, or a negated
 *                          PBTOOLS_* code.
 * @param[in] source_size   Bytes handed in. A decode that consumed a different number read a
 *                          message that does not match its own length.
 * @retval HOSTMEM_SUCCESS             Decode complete and consistent.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY The workspace was not enough. The caller enlarges it and
 *                                     calls again; nothing else about the message is wrong.
 * @retval HOSTMEM_ERROR_DECODE_FAILED Malformed message, or a length that does not add up.
 * @whisper The reading is weighed against what was offered to read
 */
hostmem_result grdw_pb_decode_verdict(int decoded_bytes, uint32_t source_size);

/**
 * @brief Translate what the generated encode reported.
 *
 * @param[out] final_size    Receives the bytes written; may be NULL. Untouched on failure.
 * @param[in]  encoded_bytes What the generated encode returned.
 * @retval HOSTMEM_SUCCESS                           Encoded, @p final_size written.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY               The workspace was not enough; enlarge and
 *                                                   call again.
 * @retval HOSTMEM_ERROR_DESTINATION_BUFFER_TO_SMALL The caller's destination could not hold the
 *                                                   message. That is a different buffer from the
 *                                                   workspace and a different thing to enlarge.
 * @retval HOSTMEM_ERROR_ENCODE_FAILED               Anything else pbtools refused on.
 * @whisper The writing is weighed against the space it was given
 */
hostmem_result grdw_pb_encode_verdict(int *final_size, int encoded_bytes);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_PB_WORKSPACE_H
