#include "gradido_blockchain_core/utils/bucket_vector.h"

#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"

#include <stdlib.h>
#include <string.h>

/*
 * The generated containers keep only their type-dependent logic inline; the primitives below
 * carry the type-independent weight of allocation and exist exactly once, however many
 * payload types a program instantiates.
 */

void *grdu_bvec_raw_alloc(size_t size, grd_memory *allocator) {
  uint8_t *buffer = NULL;
  if (!size) return NULL;
  if (size > UINT32_MAX) return NULL; // grd_alloc counts in uint32_t
  if (grd_alloc(&buffer, (uint32_t)size, allocator) != GRD_SUCCESS) return NULL;
  return buffer;
}

int grdu_bvec_allocator_reclaims(const grd_memory *allocator) {
  // no allocator means malloc/free, and default mode frees each block individually
  if (!allocator) return 1;
  return allocator->allocation_type == GRD_MEMORY_ALLOC_TYPE_DEFAULT;
}

bool grdu_bvec_raw_free(void *ptr, size_t size, grd_memory *allocator) {
  if (!ptr) return true;
  if (size > UINT32_MAX) return false;
  // strict on purpose: a warning means the arena kept the block, which _shrink must notice
  return GRD_SUCCESS == grd_free((uint8_t *)ptr, (uint32_t)size, allocator);
}

bool grdu_bvec_index_grow(
    void ***index, uint32_t old_capacity, uint32_t new_capacity, grd_memory *allocator
) {
  if (!index || !new_capacity) return false;
  // slot counts are uint32_t, their byte sizes need not fit one
  size_t new_bytes = (size_t)new_capacity * sizeof(void *);
  size_t old_bytes = (size_t)old_capacity * sizeof(void *);
  if (new_bytes > UINT32_MAX || old_bytes > UINT32_MAX) return false;
  // the warning counts as done here: an arena that had to move the block still resized it
  grd_result result =
      grd_realloc((uint8_t **)index, (uint32_t)old_bytes, (uint32_t)new_bytes, allocator);
  return GRD_SUCCESS == result || GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED == result;
}
