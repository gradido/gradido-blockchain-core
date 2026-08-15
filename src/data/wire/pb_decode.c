#include "pb_decode.h"

#include "pbtools.h"

hostmem_result grdw_pb_workspace_take(hostmem_memory_block *workspace, hostmem *allocator) {
  if (!workspace || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  // a default mode allocator has no arena to lend, and pbtools cannot work out of malloc here
  if (!allocator->capacity) { return HOSTMEM_ERROR_INVALID_PARAM; }
  return hostmem_memory_block_alloc(
      workspace, allocator->capacity - allocator->last_index, allocator
  );
}

hostmem_result grdw_pb_decode_finish(
    hostmem_memory_block *workspace,
    int used_bytes,
    int decoded_bytes,
    uint32_t source_size,
    hostmem *allocator
) {
  if (!workspace || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  // pbtools counts its heap position in int; a negative one would mean the generated code lost
  // track of its own buffer, so nothing here is trustworthy enough to keep decoding
  if (used_bytes < 0) { return HOSTMEM_ERROR_INVALID_STATE; }

  // the tail beyond what pbtools wrote goes back. A buried block cannot move its bytes and
  // keeps its recorded size, which is a finished resize, not a failure.
  hostmem_result result = hostmem_memory_block_realloc(workspace, (uint32_t)used_bytes, allocator);
  if (HOSTMEM_SUCCESS != result && HOSTMEM_WARNING_ARENA_MEMORY_NOT_RECLAIMED != result) {
    return result;
  }

  if (PBTOOLS_OUT_OF_MEMORY == -decoded_bytes) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  // a decode that stopped early or ran past the end read a message that contradicts its length
  if (decoded_bytes < 0 || (uint32_t)decoded_bytes != source_size) {
    return HOSTMEM_ERROR_DECODE_FAILED;
  }
  return HOSTMEM_SUCCESS;
}
