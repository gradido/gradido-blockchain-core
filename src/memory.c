#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Bump allocator: `last_index` walks forward through `data` and only walks back for the
 * block sitting right at it. Two invariants keep that cheap and every pointer 8 byte
 * aligned: `data` is aligned (malloc guarantees it, init_arena_static checks it), and
 * every size that moves the index goes through align8_u32 first — both directions.
 *
 * So no allocation needs padding, and alloc/free/realloc must agree on the *aligned*
 * size. A wrong old_size from a caller corrupts the arena.
 */

// true implies memory != NULL, so callers can skip their own null check
static bool is_arena(const grd_memory *memory) {
  if (!memory) return false;

  if (memory->allocation_type != GRD_MEMORY_ALLOC_TYPE_ARENA_EXTERNAL &&
      memory->allocation_type != GRD_MEMORY_ALLOC_TYPE_ARENA_OWNED) {
    return false;
  }
  return true;
}

// round up to a multiple of 8, false if that would wrap uint32_t
static bool align8_u32(uint32_t x, uint32_t *result) {
  if (x > UINT32_MAX - 7) { return false; }

  *result = ALIGN8(x);
  return true;
}

// Is this the block the bump index rests on? Only that one can be given back. Outside
// arena mode every block is owned individually, so always yes. Size must be aligned.
static bool is_reclaimable(const uint8_t *buffer, uint32_t aligned_size, const grd_memory *memory) {
  if (!is_arena(memory)) {
    return true;
  } else if (buffer && aligned_size) {
    return memory->data + memory->last_index - aligned_size == buffer;
  }
  return false;
}

// True if the request runs past the end. Records the shortfall for
// grd_memory_overflow_total(), saturating — a counter that rolls over to a small number
// is worse than one that is capped.
static bool account_capacity_exceeded(uint32_t aligned_size, grd_memory *memory) {
  // no underflow: last_index never passes capacity
  if (memory->capacity - memory->last_index < aligned_size) {
    if ((uint64_t)memory->out_of_memory_capacity + (uint64_t)aligned_size > UINT32_MAX) {
      memory->out_of_memory_capacity = UINT32_MAX;
    } else {
      memory->out_of_memory_capacity += aligned_size;
    }
    return true;
  }
  return false;
}

// ********** manage memory allocator themself *******************

grd_memory *grd_memory_create() {
  grd_memory *memory = NULL;
  if (GRD_SUCCESS != grd_alloc((uint8_t **)&memory, sizeof(grd_memory), NULL)) { return NULL; }
  // zeroed, because init inspects the previous state to find an arena it already owns
  memset(memory, 0, sizeof(grd_memory));
  return memory;
}

grd_result grd_memory_init_arena(grd_memory *memory, uint32_t capacity) {
  if (!memory) { return GRD_ERROR_NULL_POINTER; }
  if (!capacity) { return GRD_ERROR_INVALID_PARAM; }
  grd_memory_reset(memory);
  uint32_t aligned_capacity;
  if (!align8_u32(capacity, &aligned_capacity)) { return GRD_ERROR_ARITHMETIC_OVERFLOW; }
  // do we have an already existing arena?
  if (GRD_MEMORY_ALLOC_TYPE_ARENA_OWNED == memory->allocation_type && memory->capacity &&
      memory->data) {
    grd_free(memory->data, memory->capacity, NULL);
    // drop the freed pointer before anything can fail and leave it behind
    memory->data = NULL;
    memory->capacity = 0;
  }
  grd_result result = grd_alloc(&memory->data, aligned_capacity, NULL);
  if (GRD_SUCCESS != result) { return result; }
  memory->capacity = aligned_capacity;
  memory->allocation_type = GRD_MEMORY_ALLOC_TYPE_ARENA_OWNED;
  return GRD_SUCCESS;
}

grd_result grd_memory_init_arena_static(grd_memory *memory, uint8_t *data, uint32_t capacity) {
  if (!memory || !data) { return GRD_ERROR_NULL_POINTER; }
  // an unaligned base would break the "every pointer is 8 byte aligned" invariant
  if (!capacity || ALIGN8((uintptr_t)data) != (uintptr_t)data) { return GRD_ERROR_INVALID_PARAM; }
  uint32_t aligned_capacity;
  if (!align8_u32(capacity, &aligned_capacity)) { return GRD_ERROR_ARITHMETIC_OVERFLOW; }
  grd_memory_reset(memory);
  memory->data = data;
  memory->capacity = aligned_capacity;
  memory->allocation_type = GRD_MEMORY_ALLOC_TYPE_ARENA_EXTERNAL;
  return GRD_SUCCESS;
}

void grd_memory_free(grd_memory *memory) {
  if (!memory) return;
  // external arenas belong to the caller, default mode holds nothing
  if (memory->data && GRD_MEMORY_ALLOC_TYPE_ARENA_OWNED == memory->allocation_type) {
    free(memory->data);
    memory->data = NULL;
  }
  memory->capacity = 0;
  grd_memory_reset(memory);
}

void grd_memory_destroy(grd_memory *memory) {
  if (!memory) return;
  grd_memory_free(memory);
  grd_free((uint8_t *)memory, sizeof(grd_memory), NULL);
}

size_t grd_memory_overflow_total(const grd_memory *memory) {
  if (!memory) { return 0; }
  return memory->out_of_memory_capacity;
}

// ********** manage memory allocations with data ptr and size explicit *******************

grd_result grd_alloc(uint8_t **buffer, uint32_t size, grd_memory *memory) {
  if (!buffer) { return GRD_ERROR_NULL_POINTER; }
  if (!size) { return GRD_ERROR_INVALID_PARAM; }
  if (!is_arena(memory)) {
    *buffer = (uint8_t *)malloc(size);
    if (*buffer) { return GRD_SUCCESS; }
    return GRD_ERROR_OUT_OF_MEMORY;
  }
  // can only be happen, if caller access memory directly and mess with the state
  if (!memory->data) { return GRD_ERROR_INVALID_STATE; }

  // align with 8 Bytes
  uint32_t aligned_size;
  if (!align8_u32(size, &aligned_size)) { return GRD_ERROR_ARITHMETIC_OVERFLOW; }
  if (account_capacity_exceeded(aligned_size, memory)) { return GRD_ERROR_OUT_OF_MEMORY; }

  // last_index is already a multiple of 8, so no padding is needed here
  *buffer = memory->data + memory->last_index;
  memory->last_index += aligned_size;
  return GRD_SUCCESS;
}

grd_result grd_realloc(uint8_t **buffer, uint32_t old_size, uint32_t new_size, grd_memory *memory) {
  if (!buffer) { return GRD_ERROR_NULL_POINTER; }
  uint32_t new_size_aligned, old_size_aligned;
  if (!align8_u32(new_size, &new_size_aligned)) { return GRD_ERROR_ARITHMETIC_OVERFLOW; }
  if (!align8_u32(old_size, &old_size_aligned)) { return GRD_ERROR_ARITHMETIC_OVERFLOW; }

  // release on grd_free's terms and with its return value, so that freeing through here and
  // calling grd_free directly cannot drift apart. An empty buffer takes the same route.
  if (!new_size_aligned) {
    grd_result result = grd_free(*buffer, old_size_aligned, memory);
    if (GRD_SUCCESS == result) { *buffer = NULL; }
    return result;
  }

  // deliberately below the release check: (0, 0) means free, not "same size, nothing to do"
  if (*buffer && old_size == new_size) { return GRD_SUCCESS; }

  // realloc in non arena mode
  if (!is_arena(memory)) {
    // realloc(NULL, n) is malloc(n), so a fresh buffer works here too
    uint8_t *resized = (uint8_t *)realloc(*buffer, new_size);
    if (!resized) { return GRD_ERROR_OUT_OF_MEMORY; }

    *buffer = resized;
    return GRD_SUCCESS;
  }
  // an arena can only resize in place at its tail
  if (is_reclaimable(*buffer, old_size_aligned, memory)) {
    // shrink: pull the bump index back over the bytes we no longer want
    if (new_size_aligned < old_size_aligned) {
      memory->last_index -= old_size_aligned - new_size_aligned;
      return GRD_SUCCESS;
    }

    // grow: nothing is allocated behind us, so we can just claim more
    uint32_t additional = new_size_aligned - old_size_aligned;
    if (account_capacity_exceeded(additional, memory)) { return GRD_ERROR_OUT_OF_MEMORY; }
    memory->last_index += additional;

    return GRD_SUCCESS;
  }

  // buried: growing has to take a fresh block and abandon the old one until reset
  if (new_size_aligned > old_size_aligned) {
    uint8_t *resized = NULL;
    grd_result result = grd_alloc(&resized, new_size_aligned, memory);
    if (GRD_SUCCESS != result) { return result; }

    if (*buffer && old_size) { memcpy(resized, *buffer, old_size); }
    *buffer = resized;
  }

  // Reached by both buried cases: the shrink did nothing, the grow above moved the buffer
  // and left bytes behind. Either way the resize is done and the memory is not back.
  return GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED;
}

grd_result grd_clone(uint8_t **dst_buffer, const uint8_t *src, uint32_t size, grd_memory *memory) {
  if (!dst_buffer || !src) { return GRD_ERROR_NULL_POINTER; }
  if (!size) { return GRD_ERROR_INVALID_PARAM; }

  grd_result result = grd_alloc(dst_buffer, size, memory);
  if (GRD_SUCCESS != result) { return result; }

  // copy the requested size, not what an arena reserved for it
  memcpy(*dst_buffer, src, size);
  return GRD_SUCCESS;
}

grd_result grd_free(uint8_t *buffer, uint32_t size, grd_memory *memory) {
  if (!is_arena(memory)) {
    free(buffer);
    return GRD_SUCCESS;
  }

  uint32_t aligned_size;
  if (!align8_u32(size, &aligned_size)) { return GRD_ERROR_ARITHMETIC_OVERFLOW; }
  if (is_reclaimable(buffer, aligned_size, memory)) {
    memory->last_index -= aligned_size;
    return GRD_SUCCESS;
  }

  // buried in the arena: the bytes come back on reset, not now
  return GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED;
}
