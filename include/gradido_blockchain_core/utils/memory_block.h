#ifndef GRADIDO_BLOCKCHAIN_CORE_UTILS_MEMORY_BLOCK_H
#define GRADIDO_BLOCKCHAIN_CORE_UTILS_MEMORY_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../memory.h"
#include "../result.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grdu_memory_block grdu_memory_block
 *  @ingroup utils
 *  @brief A pointer and its size, kept together.
 *
 *  @ref grd_memory stores no per allocation bookkeeping, so grd_free() and
 *  grd_realloc() ask the caller for the size the buffer was allocated with. That
 *  is fine when the size is obvious at the call site and a hazard when it is not:
 *  a wrong size makes an arena move its bump index by the wrong amount.
 *
 *  A grdu_memory_block carries the size along with the pointer and updates both
 *  in one step, so the two cannot drift apart. Every function here is a thin
 *  inline wrapper over its grd_* counterpart and passes the allocator straight
 *  through — NULL still means malloc/free.
 *
 *  Warnings pass through as well. In arena mode a resize or free of anything but
 *  the most recent allocation returns @ref GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED:
 *  the bytes are not given back. What that leaves in the descriptor differs per
 *  function and is documented on each — "the resize happened" and "the bytes came
 *  back" are different questions, and they do not always have the same answer.
 *
 *  Check the warning explicitly rather than folding it into a success test.
 *  `GRD_SUCCESS != result` reads a completed resize as a failure; ignoring the
 *  warning reads memory the arena still holds as released. Which one matters
 *  depends on the call site, so it belongs at the call site.
 *  @{
 */

/** @brief A buffer and the size it was allocated with.
 *
 *  A zeroed block (@c grdu_memory_block b = {0};) is the valid empty state and
 *  what the free functions leave behind.
 *
 *  @var grdu_memory_block::data
 *  Start of the buffer, or NULL when empty. 8 byte aligned in arena mode.
 *  @var grdu_memory_block::size
 *  Bytes the caller asked for, not what an arena reserved (that is ALIGN8(size)).
 */
typedef struct grdu_memory_block {
  uint8_t *data;
  uint32_t size;
} grdu_memory_block;

/** @brief Allocate a block and record its size.
 *
 *  @param[out]    memory_block Descriptor to fill; must not be NULL. Untouched on failure.
 *  @param[in]     size         Bytes to allocate; must be > 0.
 *  @param[in,out] memory       Allocator to draw from, or NULL for malloc.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS            Block allocated, @c size recorded.
 *  @retval GRD_ERROR_NULL_POINTER @p memory_block is NULL.
 *  @retval Anything grd_alloc() can return.
 *  @note Overwrites the descriptor without looking at it; release a previous block first.
 *  @whisper A vessel carved from the flowing stream
 */
static inline grd_result grdu_memory_block_alloc(
    grdu_memory_block *memory_block, uint32_t size, grd_memory *memory
) {
  if (!memory_block) { return GRD_ERROR_NULL_POINTER; }
  grd_result result = grd_alloc(&memory_block->data, size, memory);
  if (GRD_SUCCESS == result) { memory_block->size = size; }
  return result;
}

/** @brief Release a block and clear its descriptor.
 *
 *  Passes the recorded size to grd_free(). The descriptor is only zeroed when the
 *  bytes really came back — an arena that could not reclaim leaves @p memory_block
 *  pointing at storage that stays valid until grd_memory_reset(), and says so with
 *  @ref GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED. Handle that case explicitly: the
 *  block is neither released nor safe to keep using indefinitely.
 *
 *  @param[in,out] memory_block Descriptor to release; must not be NULL.
 *  @param[in,out] memory       Allocator the block came from, or NULL for free().
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS            Block released and descriptor zeroed.
 *  @retval GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED The block was not the arena's last
 *                                 allocation, so nothing came back and the descriptor
 *                                 is left as it was. An already empty block reports
 *                                 this too, since an empty block is never the tail.
 *  @retval GRD_ERROR_NULL_POINTER @p memory_block is NULL.
 *  @note grdu_memory_block_realloc() with @c new_size 0 is interchangeable with this: same
 *        return value, same descriptor, same arena index, in every allocator mode.
 *  @whisper The vessel returns to water, form dissolving
 */
static inline grd_result grdu_memory_block_free(
    grdu_memory_block *memory_block, grd_memory *memory
) {
  if (!memory_block) { return GRD_ERROR_NULL_POINTER; }
  grd_result result = grd_free(memory_block->data, memory_block->size, memory);
  // reset only if bytes where truly deleted
  if (GRD_SUCCESS == result) {
    memory_block->data = NULL;
    memory_block->size = 0;
  }
  return result;
}

/** @brief Resize a block, keeping pointer and size in step.
 *
 *  Hands the recorded size to grd_realloc() as the old size, so the arena can tell
 *  whether this block is its tail. See grd_realloc() for what each allocator does.
 *
 *  @c size records what the block was *allocated* with, not what the caller wished
 *  for, because that is the number grd_free() and grd_realloc() have to be told
 *  later. It therefore follows whenever the allocation really changed — which is
 *  every success, plus the one warning case where the arena could not resize in
 *  place and moved the block to a fresh, larger one. It deliberately does not
 *  follow a buried shrink: nothing moved there, the arena still holds the original
 *  bytes, and recording the smaller number would make the block unreclaimable for
 *  good (a size that does not match the reservation never matches the arena tail).
 *
 *  A @p new_size of 0 releases the block on grd_free()'s terms and is interchangeable
 *  with grdu_memory_block_free() — including for an already empty block, which an arena
 *  answers with the warning either way because NULL is never its tail.
 *
 *  @param[in,out] memory_block Descriptor to resize; must not be NULL. @c data and
 *                              @c size stay consistent with the allocation.
 *  @param[in]     new_size     Requested size, or 0 to release the block.
 *  @param[in,out] memory       Allocator the block came from, or NULL for realloc.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS            Block resized and @c size updated.
 *  @retval GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED The arena kept bytes the block no
 *                                 longer needs. Not a failure. On a grow the block
 *                                 moved and @c size follows; on a shrink nothing
 *                                 happened at all and the descriptor is unchanged;
 *                                 on @p new_size 0 nothing was released.
 *  @retval GRD_ERROR_NULL_POINTER @p memory_block is NULL.
 *  @retval Anything grd_realloc() can return; the descriptor is left untouched then,
 *          so the block stays usable at its previous size.
 *  @whisper The vessel widens or narrows, the water within untouched
 */
static inline grd_result grdu_memory_block_realloc(
    grdu_memory_block *memory_block, uint32_t new_size, grd_memory *memory
) {
  if (!memory_block) { return GRD_ERROR_NULL_POINTER; }
  uint8_t *before = memory_block->data;
  grd_result result = grd_realloc(&memory_block->data, memory_block->size, new_size, memory);
  // the index moved (success), or the arena could not resize in place and handed us a
  // new block — either way the allocation changed and the recorded size has to follow.
  // Observing the pointer rather than inferring from new_size vs old keeps this right
  // even if grd_realloc ever starts moving blocks in other cases.
  if (GRD_SUCCESS == result || before != memory_block->data) { memory_block->size = new_size; }
  return result;
}

/** @brief Copy a block into a freshly allocated one.
 *
 *  @param[out]    dst    Descriptor to fill; must not be NULL. Untouched on failure.
 *  @param[in]     src    Block to copy; must not be NULL and must be non empty.
 *  @param[in,out] memory Allocator for @p dst, or NULL for malloc. Need not be the
 *                        one @p src came from.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS             Copy allocated and filled.
 *  @retval GRD_ERROR_NULL_POINTER  @p dst or @p src is NULL.
 *  @retval GRD_ERROR_INVALID_PARAM @p src is empty.
 *  @retval Anything grd_clone() can return.
 *  @whisper Water poured into a vessel newly shaped
 */
static inline grd_result grdu_memory_block_clone(
    grdu_memory_block *dst, const grdu_memory_block *src, grd_memory *memory
) {
  if (!dst || !src) { return GRD_ERROR_NULL_POINTER; }
  // size only after the clone succeeded, so a failed dst keeps its old, honest state
  grd_result result = grd_clone(&dst->data, src->data, src->size, memory);
  if (GRD_SUCCESS == result) { dst->size = src->size; }
  return result;
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_UTILS_MEMORY_BLOCK_H
