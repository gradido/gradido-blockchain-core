#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"

#include <malloc.h>

grd_result grdu_memory_init_arena(grdu_memory* memory, size_t capacity) {
    if (!memory) {
        return GRD_ERROR_NULL_POINTER;
    }
    if (!capacity) {
        return GRD_ERROR_INVALID_PARAM;
    }
    memory->data = (uint8_t*)malloc(capacity);
    if (!memory->data) {
        return GRD_ERROR_OUT_OF_MEMORY;
    }
    memory->last_index = 0;
    memory->capacity = capacity;
    memory->out_of_memory_capacity = 0;
    memory->allocation_type = GRDU_MEMORY_ALLOC_TYPE_ARENA_OWNED;
    return GRD_SUCCESS;
}

grd_result grdu_memory_init_arena_static(grdu_memory* memory, uint8_t* data, size_t capacity) {
    if (!memory || !data) {
      return GRD_ERROR_NULL_POINTER;
    }
    if (!capacity) {
        return GRD_ERROR_INVALID_PARAM;
    }
    memory->data = data;
    memory->last_index = 0;
    memory->capacity = capacity;
    memory->out_of_memory_capacity = 0;
    memory->allocation_type = GRDU_MEMORY_ALLOC_TYPE_ARENA_EXTERNAL;
    return GRD_SUCCESS;
}

grd_result grdu_memory_init_default(grdu_memory* memory)
{
    if (!memory) {
        return GRD_ERROR_NULL_POINTER;
    }
    memory->data = NULL;
    memory->last_index = 0;
    memory->capacity = 0;
    memory->out_of_memory_capacity = 0;
    memory->allocation_type = GRDU_MEMORY_ALLOC_TYPE_DEFAULT;
    return GRD_SUCCESS;
}

grd_result grdu_memory_reset(grdu_memory* memory) {
    if (!memory) {
        return GRD_ERROR_NULL_POINTER;
    }
    if (GRDU_MEMORY_ALLOC_TYPE_ARENA_OWNED == memory->allocation_type || GRDU_MEMORY_ALLOC_TYPE_ARENA_EXTERNAL == memory->allocation_type) {
        memory->last_index = 0;
        memory->out_of_memory_capacity = 0;
    }
    return GRD_SUCCESS;
}

void grdu_memory_free(grdu_memory* memory) {
    if (!memory) return;
    if (memory->data && GRDU_MEMORY_ALLOC_TYPE_ARENA_OWNED == memory->allocation_type) {
        free(memory->data);
        memory->data = NULL;
    }
    memory->last_index = 0;
    memory->capacity = 0;
    memory->out_of_memory_capacity = 0;
}

size_t grdu_memory_overflow_total(const grdu_memory* memory)
{
  if (!memory) {
    return 0;
  }
  return memory->out_of_memory_capacity;
}

grd_result grdu_memory_buffer_alloc(grdu_memory_block* memory_block, grdu_memory* memory, size_t size) {
    if (!memory || !memory_block) {
        return GRD_ERROR_NULL_POINTER;
    }
    if (!size) {
        return GRD_ERROR_INVALID_PARAM;
    }

    if (GRDU_MEMORY_ALLOC_TYPE_DEFAULT == memory->allocation_type) {
        memory_block->data = malloc(size);
        if (!memory_block->data) {
            return GRD_ERROR_OUT_OF_MEMORY;
        }
        memory_block->size = size;
        return GRD_SUCCESS;
    }
    if (!memory->data) {
        return GRD_ERROR_NOT_INITIALIZED;
    }
    if (memory->last_index + size > memory->capacity) {
        memory->out_of_memory_capacity += size;
        return GRD_ERROR_OUT_OF_MEMORY;
    }
    memory_block->data = memory->data + memory->last_index;
    memory_block->size = size;
    memory->last_index += size;
    return GRD_SUCCESS;
}

grd_result grdu_memory_buffer_free(grdu_memory_block* memory_block, grdu_memory* memory)
{
    if (!memory || !memory_block) {
        return GRD_ERROR_NULL_POINTER;
    }
    if (GRDU_MEMORY_ALLOC_TYPE_DEFAULT == memory->allocation_type) {
        free(memory_block->data);
    }
    memory_block->data = NULL;
    memory_block->size = 0;
    return GRD_SUCCESS;
}
