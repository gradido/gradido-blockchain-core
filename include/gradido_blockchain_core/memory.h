#ifndef GRADIDO_BLOCKCHAIN_CORE_MEMORY_H
#define GRADIDO_BLOCKCHAIN_CORE_MEMORY_H

#include "result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/** @defgroup grdu_memory grdu_memory
 *  @ingroup utils
 *  @brief Memory allocator supporting arena and default malloc modes.
 *
 *  A flexible allocation system offering three operational modes:
 *  - Arena with owned heap buffer: pre-allocated block via malloc, linear bump allocation
 *  - Arena with external buffer: wraps caller-provided memory, no allocation overhead
 *  - Default malloc/free: fallback to standard heap for individual allocations
 *
 *  Arena modes provide fast, deterministic allocation with O(1) bump pointer
 *  semantics. Once arena capacity is exhausted, allocation fails with
 *  GRD_ERROR_OUT_OF_MEMORY. The overflow accumulator tracks total failed
 *  requests for capacity tuning. Individual deallocation is not supported in
 *  arena modes; free the entire arena or reset for reuse.
 *
 *  Modelled after Zig's ArenaAllocator, adapted for multi-mode operation.
 *
 *  @{
 */


typedef enum grdu_memory_alloc_type {
    GRDU_MEMORY_ALLOC_TYPE_DEFAULT = 0, // simple malloc and free
    GRDU_MEMORY_ALLOC_TYPE_ARENA_OWNED = 1, // only malloc from pre-allocted buffer, fail if buffer isn't big enough
    GRDU_MEMORY_ALLOC_TYPE_ARENA_EXTERNAL = 2 // only malloc from pre-allocted buffer, fail if buffer isn't big enough
} grdu_memory_alloc_type;

/** @brief Memory allocator state container.
 *
 *  Opaque structure tracking allocation state across three operational modes.
 *  In arena modes, tracks position within a contiguous region via bump pointer.
 *  In default mode, acts as a thin wrapper around malloc/free.
 *
 *  @note All sizes are in bytes. @p out_of_memory_capacity accumulates
 *  total requested size beyond capacity in arena modes, useful for tuning.
 */ 
typedef struct grdu_memory {
    uint8_t* data;                 /**< Base of the arena (owned or external). */
    size_t last_index;             /**< Next free offset from @p data. */
    size_t capacity;               /**< Total bytes available in the arena. */
    size_t out_of_memory_capacity; /**< Accumulated overflow since last reset. */
    grdu_memory_alloc_type allocation_type;
} grdu_memory;


typedef struct grdu_memory_block {
    uint8_t* data;
    size_t   size;
} grdu_memory_block;

/** @brief Initialize arena mode with owned heap buffer.
 *
 *  Allocates @p capacity bytes via malloc and binds the arena to this buffer.
 *  The allocator owns the buffer and will free it on grdu_memory_free().
 *  All state fields (last_index, out_of_memory_capacity) reset to zero.
 *
 *  @param[in,out] memory   Allocator to initialize; must not be NULL.
 *  @param[in]     capacity Bytes to allocate; must be > 0.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS              Arena initialized and ready.
 *  @retval GRD_ERROR_NULL_POINTER   @p memory is NULL.
 *  @retval GRD_ERROR_INVALID_PARAM  @p capacity is 0.
 *  @retval GRD_ERROR_OUT_OF_MEMORY  malloc failed; buffer not allocated.
 *  @note The allocator owns the heap buffer; use grdu_memory_free() to release.
 *  @whisper Fresh soil prepared; seeds may now take root
 */
grd_result grdu_memory_init_arena(grdu_memory* memory, size_t capacity);

/** @brief Initialize arena mode with external buffer.
 *
 *  Binds the allocator to a caller-provided buffer without copying or
 *  allocation. The caller retains ownership of @p data; the allocator
 *  merely tracks position within it. Suitable for stack buffers, static
 *  storage, or memory-mapped regions.
 *
 *  @param[in,out] memory   Allocator to initialize; must not be NULL.
 *  @param[in]     data     External buffer to use as backing store; must not be NULL.
 *  @param[in]     capacity Size of @p data in bytes; must be > 0.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS             Arena bound to external buffer.
 *  @retval GRD_ERROR_NULL_POINTER  @p memory or @p data is NULL.
 *  @retval GRD_ERROR_INVALID_PARAM @p capacity is 0.
 *  @note The allocator must not outlive the external buffer it wraps.
 *  @whisper A river channel carved through known earth
 */
grd_result grdu_memory_init_arena_static(grdu_memory* memory, uint8_t* data, size_t capacity);

/** @brief Initialize default mode using malloc/free.
 *
 *  Configures the allocator as a thin wrapper around standard malloc/free.
 *  Each allocation request calls malloc individually; each free releases
 *  the specific block. No pre-allocation occurs; capacity remains zero.
 *
 *  @param[in,out] memory   Allocator to initialize; must not be NULL.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS             Allocator configured for default malloc mode.
 *  @retval GRD_ERROR_NULL_POINTER  @p memory is NULL.
 *  @whisper Open water; each drop finds its own level
 */
grd_result grdu_memory_init_default(grdu_memory* memory);

/** @brief Reset arena position to initial state.
 *
 *  In arena modes (owned or external), resets the bump pointer to zero
 *  and clears the overflow accumulator. Previously allocated blocks become
 *  invalid; do not access them after reset. In default mode, this is a
 *  no-op as allocations are managed individually.
 *
 *  @param[in,out] memory   Allocator to reset; must not be NULL.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS             Arena position reset to zero.
 *  @retval GRD_ERROR_NULL_POINTER  @p memory is NULL.
 *  @note In arena modes, all prior allocations become invalid after reset.
 *  @whisper Waters recede; the basin returns to silence
 */
grd_result grdu_memory_reset(grdu_memory* memory);

/** @brief Release allocator resources.
 *
 *  In arena-owned mode, frees the internally allocated buffer. In arena-external
 *  mode, merely drops the reference without affecting the caller's buffer.
 *  In default mode, no action is needed as individual blocks are freed
 *  separately via grdu_memory_buffer_free(). Safe to call on NULL.
 *
 *  @param[in,out] memory   Allocator to release; may be NULL.
 *  @post All internal state is zeroed; no dangling pointers remain in owned mode.
 *  @whisper Waters recede; the basin returns to silence
 */
void grdu_memory_free(grdu_memory* memory);

/** @brief Retrieve total accumulated overflow in arena modes.
 *
 *  Returns the sum of all allocation requests that exceeded arena capacity
 *  since initialization or last reset. Useful for capacity planning and
 *  tuning initial arena sizes. In default mode, always returns 0.
 *
 *  @param[in] memory   Allocator to query; may be NULL.
 *  @return Total bytes of failed requests, or 0 if @p memory is NULL
 *          or allocator is in default mode.
 *  @note The counter resets to zero on grdu_memory_reset() or re-initialization.
 *  @whisper The measure of need that exceeded the vessel
 */
size_t grdu_memory_overflow_total(const grdu_memory* memory);

/** @brief Allocate a memory block.
 *
 *  In arena modes, performs a bump-pointer allocation from the arena buffer.
 *  Fails with GRD_ERROR_OUT_OF_MEMORY if insufficient space remains; the
 *  overflow accumulator tracks the failed request size for tuning.
 *  In default mode, calls malloc for the requested size.
 *
 *  @p memory_block is overwritten regardless of prior contents. Caller must
 *  ensure any previous block data is freed (in default mode) or no longer
 *  needed (in arena modes) before calling again.
 *
 *  @param[out]    memory_block Block descriptor to fill; must not be NULL.
 *  @param[in,out] memory       Allocator to use; must not be NULL.
 *  @param[in]     size         Bytes to allocate; must be > 0.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS              Block allocated successfully.
 *  @retval GRD_ERROR_NULL_POINTER   @p memory_block or @p memory is NULL.
 *  @retval GRD_ERROR_INVALID_PARAM  @p size is 0.
 *  @retval GRD_ERROR_NOT_INITIALIZED Allocator not initialized (arena modes without buffer).
 *  @retval GRD_ERROR_OUT_OF_MEMORY  Arena exhausted or malloc failed.
 *  @note In arena modes, individual blocks cannot be freed separately.
 *  @whisper A vessel carved from the flowing stream
 */
grd_result grdu_memory_buffer_alloc(grdu_memory_block* memory_block, grdu_memory* memory, size_t size);

/** @brief Free a memory block.
 *
 *  In default mode, calls free() on the block's data pointer.
 *  In arena modes, only nullifies the block descriptor without freeing
 *  memory, as arena deallocation occurs via grdu_memory_reset() or
 *  grdu_memory_free(). Always safe to call; idempotent on NULL data.
 *
 *  @param[in,out] memory_block Block to release; must not be NULL.
 *  @param[in]     memory       Allocator used for allocation; must not be NULL.
 *  @return grd_result indicating success or failure type.
 *  @retval GRD_SUCCESS             Block descriptor cleared.
 *  @retval GRD_ERROR_NULL_POINTER @p memory_block or @p memory is NULL.
 *  @note In arena modes, this does not reclaim space; reset the arena instead.
 *  @whisper The vessel returns to water, form dissolving
 */
grd_result grdu_memory_buffer_free(grdu_memory_block* memory_block, grdu_memory* memory);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MEMORY_H
