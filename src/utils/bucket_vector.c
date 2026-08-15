#include "gradido_blockchain_core/utils/bucket_vector.h"

#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"

/*
 * The generated containers keep only their type-dependent logic inline; the primitives below
 * carry the type-independent weight of allocation and exist exactly once, however many
 * payload types a program instantiates.
 *
 * Everything here counts in the allocator's uint32_t. Sizes that still had to be narrowed at
 * runtime are gone: a bucket's byte size is settled at compile time by the static assert in
 * GRDU_BVEC_DECLARE, and the index array is the only place where a slot count still turns
 * into bytes — which happens below, once.
 */

/** Slots to bytes. The callers keep capacities inside the bound checked in _index_grow. */
static uint32_t index_bytes(uint32_t capacity) {
  return (uint32_t)((size_t)capacity * sizeof(void *));
}

void *grdu_bvec_raw_alloc(uint32_t size, grd_memory *allocator) {
  uint8_t *buffer = NULL;
  // grd_alloc rejects a zero size itself, so an empty request simply arrives back as NULL
  if (GRD_SUCCESS != grd_alloc(&buffer, size, allocator)) return NULL;
  return buffer;
}

bool grdu_bvec_raw_free(void *ptr, uint32_t size, grd_memory *allocator) {
  if (!ptr) return true;
  // strict on purpose: a warning means the arena kept the block, which _shrink must notice
  return GRD_SUCCESS == grd_free((uint8_t *)ptr, size, allocator);
}

bool grdu_bvec_index_grow(
    void ***index, uint32_t old_capacity, uint32_t new_capacity, grd_memory *allocator
) {
  if (!index || !new_capacity) return false;
  // slot counts are uint32_t, the byte size they stand for need not be; this is the gate that
  // decides it, and old_capacity passed through it when it was granted
  if (new_capacity > UINT32_MAX / sizeof(void *)) return false;
  // the warning counts as done here: an arena that had to move the block still resized it
  grd_result result = grd_realloc(
      (uint8_t **)index, index_bytes(old_capacity), index_bytes(new_capacity), allocator
  );
  if (GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED == result) {
    if (new_capacity > old_capacity) { return true; }
    return false;
  }
  return GRD_SUCCESS == result;
}

bool grdu_bvec_index_free(void **index, uint32_t capacity, grd_memory *allocator) {
  return grdu_bvec_raw_free(index, index_bytes(capacity), allocator);
}
