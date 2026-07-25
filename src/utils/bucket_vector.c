#include "gradido_blockchain_core/utils/bucket_vector.h"

#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"

#include <stdlib.h>
#include <string.h>

/**
 * The generated containers keep only their type-dependent logic inline; the three primitives
 * below carry the type-independent weight of allocation and therefore exist exactly once,
 * regardless of how many payload types a program instantiates.
 */

void *grdu_bvec_raw_alloc(size_t size, grd_memory *allocator) {
  uint8_t *buffer = NULL;
  if (!size) return NULL;
  if (!allocator) return malloc(size);
  if (grd_memory_buffer_alloc(&buffer, allocator, size) != GRD_SUCCESS) return NULL;
  return buffer;
}

void grdu_bvec_raw_free(void *ptr, grd_memory *allocator) {
  if (!ptr) return;
  if (!allocator) {
    free(ptr);
    return;
  }
  grd_memory_buffer_free((uint8_t *)ptr, allocator);
}

void **grdu_bvec_index_grow(
    void **old_index, size_t used, size_t new_capacity, grd_memory *allocator
) {
  void **grown = NULL;
  if (!new_capacity || new_capacity > SIZE_MAX / sizeof(void *)) return NULL;
  if (!allocator) {
    // realloc carries the pointers over and usually keeps the block in place
    return (void **)realloc(old_index, new_capacity * sizeof(void *));
  }
  grown = (void **)grdu_bvec_raw_alloc(new_capacity * sizeof(void *), allocator);
  if (!grown) return NULL;
  if (old_index && used) {
    memcpy(grown, old_index, used * sizeof(void *));
    // no-op in arena modes: the superseded array stays behind until the arena resets
    grdu_bvec_raw_free(old_index, allocator);
  }
  return grown;
}
