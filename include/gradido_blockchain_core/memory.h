#ifndef GRADIDO_BLOCKCHAIN_CORE_MEMORY_H
#define GRADIDO_BLOCKCHAIN_CORE_MEMORY_H

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
 *  These rules hold for every function below:
 *
 *  - The allocator is the last argument and NULL is valid there: it means
 *    malloc/free, same as @ref GRD_MEMORY_ALLOC_TYPE_DEFAULT. A call site picks a
 *    strategy by what it passes, not by which function it calls.
 *  - Sizes are passed in, never stored, so freeing and resizing need the size the
 *    caller allocated with. A wrong size makes the arena move its index by the
 *    wrong amount. @ref grdu_memory_block (utils/memory_block.h) keeps pointer and
 *    size together when that bookkeeping should not be the caller's job.
 *  - Every size rounds up to a multiple of 8, which keeps all returned pointers
 *    8 byte aligned. One that would wrap uint32_t yields GRD_ERROR_ARITHMETIC_OVERFLOW.
 *  - An arena can only give back its most recent allocation; anything before it stays
 *    reserved until @ref grd_memory_reset. Calls that could not reclaim return
 *    @ref GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED — the operation happened, the memory
 *    did not come back. Handle it explicitly; it is neither a failure nor a release.
 *  - Failures leave every output untouched.
 *
 *  @{
 */

/* Alignment: Most CPUs access memory more efficiently if data starts at
 * addresses that are multiples of 4 or 8. We'll align to 8 bytes.
 * This macro takes a size 'x' and rounds it UP to the nearest multiple of 8.
 * Example: ALIGN8(3) -> 8, ALIGN8(8) -> 8, ALIGN8(10) -> 16
 */
#define ALIGN8(x) (((x) + 7) & (~7))

/** @brief Allocation strategy and ownership of the arena buffer. */
typedef enum grd_memory_alloc_type {
  GRD_MEMORY_ALLOC_TYPE_DEFAULT = 0,   /**< Individual malloc/free per allocation. */
  GRD_MEMORY_ALLOC_TYPE_ARENA_OWNED,   /**< Bump allocator, buffer owned by the allocator. */
  GRD_MEMORY_ALLOC_TYPE_ARENA_EXTERNAL /**< Bump allocator, buffer owned by the caller. */
} grd_memory_alloc_type;

/** @brief Memory allocator state container.
 *
 *  The two init functions write every field and read none, so they accept uninitialized
 *  storage — @c grd_memory mem; followed by an init is correct. Everything else needs an
 *  allocator that is either initialized or zeroed; @c grd_memory mem = {0}; is the valid
 *  empty state and means default mode (malloc/free).
 *
 *  @note Do not write these fields; @p last_index and @p capacity are kept multiples of 8.
 */
typedef struct grd_memory {
  uint8_t *data;                   /**< Base of the arena (owned or external), 8 byte aligned. */
  uint32_t last_index;             /**< Next free offset from @p data. */
  uint32_t capacity;               /**< Bytes available in the arena, rounded up to 8. */
  uint32_t out_of_memory_capacity; /**< Accumulated overflow since last reset, saturating. */
  uint8_t allocation_type;         /**< grd_memory_alloc_type, one byte is enough. */
} grd_memory;

// ********** manage memory allocator themself *******************

/** @brief Allocate and zero a grd_memory on the heap, ready for an init call.
 *
 *  @return Zeroed allocator in default mode, or NULL when the heap is exhausted.
 *  @note Pair with grd_memory_destroy().
 *  @whisper A vessel for vessels, itself drawn from the stream
 */
grd_memory *grd_memory_create();

/** @brief Initialize arena mode with an owned heap buffer.
 *
 *  Writes every field and reads none, so uninitialized storage is a valid input.
 *
 *  @param[in,out] memory   Allocator to initialize; not NULL. Need not be zeroed.
 *  @param[in]     capacity Bytes to reserve, rounded up to 8; must be > 0.
 *  @retval GRD_SUCCESS             Arena ready, bump index at 0.
 *  @retval GRD_ERROR_NULL_POINTER  @p memory is NULL.
 *  @retval GRD_ERROR_INVALID_PARAM @p capacity is 0.
 *  @retval GRD_ERROR_OUT_OF_MEMORY malloc failed; @p memory is left untouched.
 *  @warning Calling this on an allocator that already owns an arena leaks that buffer.
 *           Use grd_memory_reinit_arena() to replace one.
 *  @whisper The basin is dug, and waits for water
 */
grd_result grd_memory_init_arena(grd_memory *memory, uint32_t capacity);

/** @brief Initialize arena mode with a caller owned buffer.
 *
 *  Borrows @p data; grd_memory_free() will not release it. Suited to stack or static
 *  storage that outlives the allocator. Like grd_memory_init_arena() it writes every field
 *  and reads none.
 *
 *  Both @p data and @p capacity must be multiples of 8 and are rejected otherwise, rather
 *  than rounded. Rounding a capacity up would let the arena bump past the end of a buffer
 *  the caller sized exactly — seven bytes of silent corruption is not worth the convenience.
 *
 *  Re-initializing over another external arena needs nothing else: there is no buffer to
 *  give back, so just call this again. Only an allocator that currently owns a *heap* arena
 *  has something to release first — grd_memory_free() it before switching to an external
 *  buffer, or it leaks.
 *
 *  @param[in,out] memory   Allocator to initialize; not NULL. Need not be zeroed.
 *  @param[in]     data     Buffer to bump through; not NULL, 8 byte aligned (@c alignas(8)).
 *  @param[in]     capacity Usable bytes in @p data; must be > 0 and a multiple of 8.
 *  @retval GRD_SUCCESS             Arena ready, bump index at 0.
 *  @retval GRD_ERROR_NULL_POINTER  @p memory or @p data is NULL.
 *  @retval GRD_ERROR_INVALID_PARAM @p capacity is 0 or not a multiple of 8, or @p data is
 *                                  not 8 byte aligned.
 *  @whisper Borrowed ground, returned unbroken
 */
grd_result grd_memory_init_arena_static(grd_memory *memory, uint8_t *data, uint32_t capacity);

/** @brief Drop every outstanding allocation at once, keeping the buffer. O(1).
 *
 *  Moves the bump index back to 0 and clears the overflow counter. Nothing to do in
 *  default mode.
 *
 *  @param[in,out] memory Allocator to rewind; may be NULL.
 *  @warning Every pointer this arena handed out dangles afterwards.
 *  @whisper The tide goes out, the basin whole again
 */
static inline void grd_memory_reset(grd_memory *memory) {
  if (memory) {
    memory->last_index = 0;
    memory->out_of_memory_capacity = 0;
  }
}

/** @brief Release what the allocator owns, but not the allocator itself.
 *
 *  Frees the buffer in @ref GRD_MEMORY_ALLOC_TYPE_ARENA_OWNED mode and rewinds. External
 *  buffers stay untouched, default mode holds nothing. @p allocation_type is kept, so the
 *  allocator stays the kind it was.
 *
 *  @param[in,out] memory Allocator to empty; may be NULL.
 *  @whisper Waters recede; the basin returns to silence
 */
void grd_memory_free(grd_memory *memory);

/** @brief Replace the arena of an allocator that already has one.
 *
 *  grd_memory_free() followed by grd_memory_init_arena(): releases what is held, then
 *  reserves @p capacity fresh bytes. This is the call that resizes an arena.
 *
 *  Unlike the init functions this one reads @p memory, so it needs an allocator that was
 *  initialized before, or a zeroed one (where the free half simply does nothing).
 *
 *  @param[in,out] memory   Initialized or zeroed allocator; not NULL.
 *  @param[in]     capacity Bytes for the new arena, rounded up to 8; must be > 0.
 *  @return Whatever grd_memory_init_arena() returns. On failure the old arena is already
 *          gone and @p memory is left empty in its previous mode.
 *  @warning Every pointer the old arena handed out dangles afterwards.
 *  @whisper The basin emptied, then dug anew
 */
static inline grd_result grd_memory_reinit_arena(grd_memory *memory, uint32_t capacity) {
  grd_memory_free(memory);
  return grd_memory_init_arena(memory, capacity);
}

/** @brief grd_memory_free(), then release the grd_memory itself.
 *
 *  @param[in] memory From grd_memory_create(), never stack or static storage; may be NULL.
 *  @whisper The vessel that held vessels returns to the stream
 */
void grd_memory_destroy(grd_memory *memory);

/** @brief Bytes worth of requests that did not fit since the last reset.
 *
 *  Says how much larger the arena would have to be. Saturates at UINT32_MAX instead of
 *  wrapping, and is always 0 in default mode. grd_memory_reset() clears it.
 *
 *  @param[in] memory Allocator to query; may be NULL.
 *  @return Total bytes of failed requests, or 0 if @p memory is NULL.
 *  @whisper The measure of need that exceeded the vessel
 */
size_t grd_memory_overflow_total(const grd_memory *memory);

// ********** manage memory allocations with data ptr and size explicit *******************

/** @brief Allocate a raw buffer: malloc, or a bump of the arena index.
 *
 *  @param[out]    buffer Receives the allocation; not NULL.
 *  @param[in]     size   Bytes to allocate; must be > 0. An arena reserves ALIGN8(size).
 *  @param[in,out] memory Allocator to draw from, or NULL for malloc.
 *  @retval GRD_SUCCESS             Buffer allocated.
 *  @retval GRD_ERROR_NULL_POINTER  @p buffer is NULL.
 *  @retval GRD_ERROR_INVALID_PARAM @p size is 0.
 *  @retval GRD_ERROR_INVALID_STATE Arena mode without a buffer: never initialized, or the
 *                                  fields were written directly.
 *  @retval GRD_ERROR_OUT_OF_MEMORY malloc failed, or the arena is full — the shortfall goes
 *                                  to grd_memory_overflow_total().
 *  @note The memory is not zeroed and holds whatever the previous tenant left.
 *  @whisper Raw earth shaped by the hand of need
 */
grd_result grd_alloc(uint8_t **buffer, uint32_t size, grd_memory *memory);

/** @brief Resize a buffer, in place where the allocator allows it.
 *
 *  Outside arena mode this is realloc(): the block may move, contents are preserved. In
 *  arena mode only the tail block can touch the bump index, so:
 *
 *  | block    | direction | what happens                            | returns |
 *  |----------|-----------|-----------------------------------------|---------|
 *  | tail     | shrink    | index moves back, bytes reusable        | SUCCESS |
 *  | tail     | grow      | index moves on, address unchanged       | SUCCESS |
 *  | non tail | grow      | fresh block, @p old_size bytes copied   | WARNING |
 *  | non tail | shrink    | nothing at all, address and bytes kept  | WARNING |
 *
 *  @p new_size 0 releases the block through grd_free() and is interchangeable with it,
 *  down to the return value: @c *buffer is cleared only when the bytes really came back.
 *
 *  @param[in,out] buffer   Not NULL, but may point to NULL to allocate from scratch.
 *                          Updated when the block moves.
 *  @param[in]     old_size Size the buffer was allocated with; only the arena needs it, to
 *                          recognize the tail.
 *  @param[in]     new_size Requested size, or 0 to free.
 *  @param[in,out] memory   Allocator the buffer came from, or NULL for realloc.
 *  @retval GRD_SUCCESS            Resized, or the sizes were already equal.
 *  @retval GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED Per the table. On @p new_size 0 it means
 *                                 the block was not released at all.
 *  @retval GRD_ERROR_NULL_POINTER @p buffer is NULL.
 *  @retval GRD_ERROR_OUT_OF_MEMORY realloc failed, or the arena has no room to grow.
 *  @whisper The vessel widens or narrows, the water within untouched
 */
grd_result grd_realloc(uint8_t **buffer, uint32_t old_size, uint32_t new_size, grd_memory *memory);

/** @brief grd_alloc() plus memcpy; copies exactly @p size bytes.
 *
 *  @param[out]    dst_buffer Receives the copy; not NULL.
 *  @param[in]     src        Source holding @p size bytes; not NULL.
 *  @param[in]     size       Bytes to copy; must be > 0.
 *  @param[in,out] memory     Allocator to draw from, or NULL for malloc.
 *  @retval GRD_SUCCESS             Copy allocated and filled.
 *  @retval GRD_ERROR_NULL_POINTER  @p dst_buffer or @p src is NULL.
 *  @retval GRD_ERROR_INVALID_PARAM @p size is 0.
 *  @retval Anything grd_alloc() can return.
 *  @whisper Water poured into a vessel newly shaped
 */
grd_result grd_clone(uint8_t **dst_buffer, const uint8_t *src, uint32_t size, grd_memory *memory);

/** @brief Free a buffer: free(), or move the arena index back if it is the tail.
 *
 *  @param[in]     buffer Buffer to release; may be NULL, which changes nothing — though an
 *                        arena still warns, since NULL is never its tail.
 *  @param[in]     size   Size the buffer was allocated with. Ignored outside arena mode.
 *  @param[in,out] memory Allocator the buffer came from, or NULL for free().
 *  @retval GRD_SUCCESS Buffer freed, or the arena reclaimed its bytes.
 *  @retval GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED Not the arena's last allocation, so the
 *                      block is still there — do not treat it as released.
 *  @warning A @p size that does not match the allocation moves the index by the wrong
 *           amount and hands the same bytes out twice.
 *  @whisper Form dissolves, substance returning to source
 */
grd_result grd_free(uint8_t *buffer, uint32_t size, grd_memory *memory);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MEMORY_H
