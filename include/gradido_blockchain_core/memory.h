#ifndef GRADIDO_BLOCKCHAIN_CORE_MEMORY_H
#define GRADIDO_BLOCKCHAIN_CORE_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grd_memory grd_memory
 *  @ingroup utils
 *  @brief Allocator that is either a bump arena or plain malloc/free.
 *
 *  Every allocating call takes a @ref grd_memory as its last argument and NULL
 *  is a valid value there: it means malloc/free. The same call site therefore
 *  serves both strategies, and a caller decides between them by what it passes,
 *  not by which function it calls.
 *
 *  Sizes are passed in, never stored. The allocator keeps a bump index and
 *  nothing else, so freeing and resizing need the size the caller allocated with
 *  — see @ref grd_free and @ref grd_realloc. @ref grdu_memory_block in
 *  utils/memory_block.h pairs pointer and size when that bookkeeping should not
 *  be the caller's job.
 *
 *  In arena mode only the most recent allocation can be given back; anything
 *  before it stays reserved until @ref grd_memory_reset. Calls that could not
 *  reclaim say so with @ref GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED — a warning,
 *  not a failure: the operation itself succeeded.
 *
 *  @{
 */

/* Alignment: Most CPUs access memory more efficiently if data starts at
 * addresses that are multiples of 4 or 8. We'll align to 8 bytes.
 * This macro takes a size 'x' and rounds it UP to the nearest multiple of 8.
 * Example: ALIGN8(3) -> 8, ALIGN8(8) -> 8, ALIGN8(10) -> 16
 */
#define ALIGN8(x) (((x) + 7) & (~7))

/** @brief Operational mode for memory allocator.
 *
 *  Determines allocation strategy and ownership semantics.
 */
typedef enum grd_memory_alloc_type {
  GRD_MEMORY_ALLOC_TYPE_DEFAULT = 0, /**< Individual malloc/free per allocation. */
  GRD_MEMORY_ALLOC_TYPE_ARENA_OWNED =
      1, /**< Bump allocator with heap-allocated buffer owned by the allocator. */
  GRD_MEMORY_ALLOC_TYPE_ARENA_EXTERNAL =
      2 /**< Bump allocator with caller-provided external buffer. */
} grd_memory_alloc_type;

/** @brief Memory allocator state container.
 *
 *  @warning Must be zero initialized before the first grd_memory_init_arena() or
 *  grd_memory_init_arena_static() call — write @c grd_memory mem = {0}; (or use
 *  grd_memory_create()). Init inspects the previous state to release an arena it
 *  already owns, so uninitialized stack bytes would be read as a live allocation.
 *
 *  @note @p last_index and @p capacity are kept as multiples of 8, so every
 *  handed out pointer is 8 byte aligned. Do not write these fields directly.
 */
typedef struct grd_memory {
  uint8_t *data;                   /**< Base of the arena (owned or external), 8 byte aligned. */
  uint32_t last_index;             /**< Next free offset from @p data, always a multiple of 8. */
  uint32_t capacity;               /**< Total bytes available in the arena, rounded up to 8. */
  uint32_t out_of_memory_capacity; /**< Accumulated overflow since last reset, saturating. */
  uint8_t allocation_type; // grd_memory_alloc_type but as uint8_t because we don't need more than 1
                           // Byte for this
} grd_memory;

// ********** manage memory allocator themself *******************

/** @brief Allocate and zero a grd_memory on the heap.
 *
 *  The result is in default mode (malloc/free) and ready for an init call.
 *  Pair with grd_memory_destroy(), which also releases an owned arena.
 *
 *  @return Zeroed allocator, or NULL when the heap is exhausted.
 *  @whisper A vessel for vessels, itself drawn from the stream
 */
grd_memory *grd_memory_create();

/** @brief Initialize arena mode with owned heap buffer.
 *
 *  Rounds @p capacity up to a multiple of 8 and takes that many bytes from the
 *  heap. Calling this on an allocator that already owns an arena releases the old
 *  buffer first, so re-sizing an arena is a single call.
 *
 *  @param[in,out] memory   Allocator to initialize; must not be NULL and must be
 *                          zero initialized before its first init (see @ref grd_memory).
 *  @param[in]     capacity Bytes to reserve; must be > 0.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS                 Arena ready, bump index at 0.
 *  @retval GRD_ERROR_NULL_POINTER      @p memory is NULL.
 *  @retval GRD_ERROR_INVALID_PARAM     @p capacity is 0.
 *  @retval GRD_ERROR_ARITHMETIC_OVERFLOW Rounding @p capacity up to 8 would wrap uint32_t.
 *  @retval GRD_ERROR_OUT_OF_MEMORY     malloc failed; the old arena is already gone.
 *  @whisper The basin is dug, and waits for water
 */
grd_result grd_memory_init_arena(grd_memory *memory, uint32_t capacity);

/** @brief Initialize arena mode with external buffer.
 *
 *  Borrows @p data without taking ownership — grd_memory_free() will not release
 *  it. Suited to stack or static storage that outlives the allocator.
 *
 *  @param[in,out] memory   Allocator to initialize; must not be NULL and must be
 *                          zero initialized before its first init.
 *  @param[in]     data     Buffer to bump through; must not be NULL and must be
 *                          8 byte aligned (@c alignas(8) on the definition).
 *  @param[in]     capacity Usable bytes in @p data; must be > 0. Rounded up to a
 *                          multiple of 8, so @p data must be that large.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS                 Arena ready, bump index at 0.
 *  @retval GRD_ERROR_NULL_POINTER      @p memory or @p data is NULL.
 *  @retval GRD_ERROR_INVALID_PARAM     @p capacity is 0, or @p data is not 8 byte aligned.
 *  @retval GRD_ERROR_ARITHMETIC_OVERFLOW Rounding @p capacity up to 8 would wrap uint32_t.
 *  @warning Rounding up means an unaligned @c capacity lets the arena hand out up to
 *           7 bytes past what the caller declared. Size the buffer to a multiple of 8.
 *  @whisper Borrowed ground, returned unbroken
 */
grd_result grd_memory_init_arena_static(grd_memory *memory, uint8_t *data, uint32_t capacity);

/** @brief Reset arena position to initial state.
 *
 *  Drops every outstanding allocation at once by moving the bump index back to 0
 *  and clearing the overflow counter. The arena buffer itself is kept, so the
 *  next allocation reuses it. O(1), and a no-op worth calling on a default mode
 *  allocator (there is nothing to move).
 *
 *  @param[in,out] memory Allocator to rewind; may be NULL.
 *  @warning Every pointer previously handed out by this arena dangles afterwards.
 *  @whisper The tide goes out, the basin whole again
 */
static inline void grd_memory_reset(grd_memory *memory) {
  if (memory) {
    memory->last_index = 0;
    memory->out_of_memory_capacity = 0;
  }
}

/** @brief Release allocator resources.
 *
 *  Frees the arena buffer in @ref GRD_MEMORY_ALLOC_TYPE_ARENA_OWNED mode and
 *  rewinds the state. External buffers are left untouched, and in default mode
 *  there is nothing held to release. Releases what the allocator owns, not the
 *  allocator itself — see grd_memory_destroy() for that.
 *
 *  @param[in,out] memory Allocator to empty; may be NULL.
 *  @note @p allocation_type is kept, so the allocator stays the kind it was.
 *  @whisper Waters recede; the basin returns to silence
 */
void grd_memory_free(grd_memory *memory);

/** @brief Release an allocator obtained from grd_memory_create().
 *
 *  Calls grd_memory_free() and then releases the grd_memory itself.
 *
 *  @param[in] memory Allocator to destroy; may be NULL. Must come from
 *                    grd_memory_create(), never from stack or static storage.
 *  @whisper The vessel that held vessels returns to the stream
 */
void grd_memory_destroy(grd_memory *memory);

/** @brief Retrieve total accumulated overflow in arena modes.
 *
 *  Sums the (8 byte aligned) sizes of all requests that did not fit since the
 *  last reset, which says how much larger the arena would have to be. Saturates
 *  at UINT32_MAX rather than wrapping. Always 0 in default mode, where a request
 *  either succeeds or the heap itself is exhausted.
 *
 *  @param[in] memory Allocator to query; may be NULL.
 *  @return Total bytes of failed requests, or 0 if @p memory is NULL.
 *  @note grd_memory_reset() clears the counter.
 *  @whisper The measure of need that exceeded the vessel
 */
size_t grd_memory_overflow_total(const grd_memory *memory);

// ********** manage memory allocations with data ptr and size explicit *******************

/** @brief Allocate a raw buffer.
 *
 *  With @p memory NULL or in default mode this is malloc(). In arena mode it is a
 *  bump: @p size is rounded up to a multiple of 8 and the index moves on, which
 *  keeps every returned pointer 8 byte aligned.
 *
 *  @param[out]    buffer Receives the allocation; must not be NULL. Untouched on failure.
 *  @param[in]     size   Bytes to allocate; must be > 0. The arena reserves ALIGN8(size).
 *  @param[in,out] memory Allocator to draw from, or NULL for malloc.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS                   Buffer allocated.
 *  @retval GRD_ERROR_NULL_POINTER        @p buffer is NULL.
 *  @retval GRD_ERROR_INVALID_PARAM       @p size is 0.
 *  @retval GRD_ERROR_INVALID_STATE       Arena mode without a buffer — the allocator
 *                                        was never initialized, or its fields were written
 * directly.
 *  @retval GRD_ERROR_ARITHMETIC_OVERFLOW Rounding @p size up to 8 would wrap uint32_t.
 *  @retval GRD_ERROR_OUT_OF_MEMORY       malloc failed, or the arena has no room left;
 *                                        in arena mode the shortfall is added to the overflow
 * counter.
 *  @note Arena memory is not zeroed and holds whatever the previous tenant left.
 *  @whisper Raw earth shaped by the hand of need
 */
grd_result grd_alloc(uint8_t **buffer, uint32_t size, grd_memory *memory);

/** @brief Resize a buffer, in place where the allocator allows it.
 *
 *  With @p memory NULL or in default mode this is realloc(): the block may move
 *  and its contents are preserved. In arena mode the outcome depends on where the
 *  buffer sits, because only the most recent allocation touches the bump index:
 *  - tail block, shrinking: the index moves back and the bytes are reusable.
 *  - tail block, growing: the index moves on, the buffer keeps its address.
 *  - non tail, growing: a fresh block is taken and @p old_size bytes are copied
 *    over; the old block stays reserved until reset.
 *  - non tail, shrinking: nothing happens at all. @p buffer keeps its address and
 *    its bytes, and the arena keeps the full reservation.
 *
 *  The last two cases return @ref GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED, so a
 *  caller that only wanted the resize can treat it as done, while a caller that
 *  wanted the memory back learns it did not get it.
 *
 *  Passing @p new_size 0 is a free: the block goes back through grd_free(), and
 *  @c *buffer is cleared on the same terms that function uses — only when the
 *  bytes really came back. A buried arena block therefore leaves @c *buffer as it
 *  was and reports the warning, because the block is still there. An empty
 *  @c *buffer takes the same route and gets the same answer, so releasing through
 *  this function and through grd_free() are interchangeable.
 *
 *  @param[in,out] buffer   Buffer to resize; must not be NULL, but may point to
 *                          NULL to allocate from scratch. Updated when the block moves.
 *  @param[in]     old_size Size the buffer was allocated with. Only the arena needs it,
 *                          to recognize the tail; realloc() gets it from the heap.
 *  @param[in]     new_size Requested size, or 0 to free.
 *  @param[in,out] memory   Allocator the buffer came from, or NULL for realloc.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS                   Resized, or @p old_size already equals @p new_size.
 *  @retval GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED Resize done, but the arena kept the
 *                                        old bytes. Not a failure. On a @p new_size of 0
 *                                        it means the block was not released at all.
 *  @retval GRD_ERROR_NULL_POINTER        @p buffer is NULL.
 *  @retval GRD_ERROR_ARITHMETIC_OVERFLOW Rounding a size up to 8 would wrap uint32_t.
 *  @retval GRD_ERROR_OUT_OF_MEMORY       realloc failed, or the arena has no room to
 *                                        grow; @c *buffer stays valid and unchanged either way.
 *  @warning An @p old_size that does not match the allocation makes the arena
 *           misjudge the tail and move its index by the wrong amount.
 *  @whisper The vessel widens or narrows, the water within untouched
 */
grd_result grd_realloc(uint8_t **buffer, uint32_t old_size, uint32_t new_size, grd_memory *memory);

/** @brief Allocate a buffer and copy @p size bytes into it.
 *
 *  grd_alloc() followed by memcpy; the copy is exactly @p size bytes even though
 *  an arena reserves ALIGN8(size).
 *
 *  @param[out]    dst_buffer Receives the copy; must not be NULL. Untouched on failure.
 *  @param[in]     src        Source to read from; must not be NULL and must hold @p size bytes.
 *  @param[in]     size       Bytes to copy; must be > 0.
 *  @param[in,out] memory     Allocator to draw from, or NULL for malloc.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS             Copy allocated and filled.
 *  @retval GRD_ERROR_NULL_POINTER  @p dst_buffer or @p src is NULL.
 *  @retval GRD_ERROR_INVALID_PARAM @p size is 0.
 *  @retval Anything grd_alloc() can return.
 *  @whisper Water poured into a vessel newly shaped
 */
grd_result grd_clone(uint8_t **dst_buffer, const uint8_t *src, uint32_t size, grd_memory *memory);

/** @brief Free a buffer.
 *
 *  With @p memory NULL or in default mode this is free(), and @p size is ignored.
 *  In arena mode the bump index moves back only when @p buffer is the most recent
 *  allocation; otherwise the bytes stay reserved until grd_memory_reset() and the
 *  call reports @ref GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED.
 *
 *  @param[in] buffer Buffer to release; may be NULL. Nothing happens then, though an
 *                    arena still answers with the warning, since NULL is never its tail.
 *  @param[in] size   Size the buffer was allocated with. Ignored outside arena mode.
 *  @param[in,out] memory Allocator the buffer came from, or NULL for free().
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS                   Buffer freed, or the arena reclaimed its bytes.
 *  @retval GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED @p buffer was not the arena's last
 *                                        allocation, so nothing came back. Not a failure,
 *                                        but the block is still there — do not treat it as
 * released.
 *  @retval GRD_ERROR_ARITHMETIC_OVERFLOW Rounding @p size up to 8 would wrap uint32_t.
 *  @warning A @p size that does not match the allocation moves the arena index by the
 *           wrong amount and hands the same bytes out twice.
 *  @whisper Form dissolves, substance returning to source
 */
grd_result grd_free(uint8_t *buffer, uint32_t size, grd_memory *memory);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MEMORY_H
