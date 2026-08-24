#include "pb_decode.h"

#include "arnm/arena.h"

#include "pbtools.h"

arnm_result grdw_pb_workspace_take(arnm_memory_block *workspace, arnm *allocator) {
  if (!workspace || !allocator) { return ARNM_ERROR_NULL_POINTER; }
  // a default mode allocator has no arena to lend, and pbtools cannot work out of malloc here
  if (!arnm_is_arena(allocator)) { return ARNM_ERROR_INVALID_PARAM; }
  // An arena with nothing left is out of memory, not a bad argument. Left to arnm_alloc it
  // would arrive as a zero sized request and come back as INVALID_PARAM, sending the caller to
  // inspect arguments that were all correct.
  const uint32_t remaining = arnm_arena_remaining(allocator);
  if (!remaining) { return ARNM_ERROR_OUT_OF_MEMORY; }
  return arnm_memory_block_alloc(workspace, remaining, allocator);
}

arnm_result grdw_pb_decode_finish(
    arnm_memory_block *workspace,
    int used_bytes,
    int decoded_bytes,
    uint32_t source_size,
    arnm *allocator
) {
  if (!workspace || !allocator) { return ARNM_ERROR_NULL_POINTER; }
  // pbtools counts its heap position in int; a negative one would mean the generated code lost
  // track of its own buffer, so nothing here is trustworthy enough to keep decoding
  if (used_bytes < 0) { return ARNM_ERROR_INVALID_STATE; }

  // the tail beyond what pbtools wrote goes back. A buried block cannot move its bytes and
  // keeps its recorded size, which is a finished resize, not a failure.
  arnm_result result = arnm_memory_block_realloc(workspace, (uint32_t)used_bytes, allocator);
  if (ARNM_SUCCESS != result && ARNM_WARNING_ARENA_MEMORY_NOT_RECLAIMED != result) {
    return result;
  }

  if (PBTOOLS_OUT_OF_MEMORY == -decoded_bytes) { return ARNM_ERROR_OUT_OF_MEMORY; }
  // a decode that stopped early or ran past the end read a message that contradicts its length
  if (decoded_bytes < 0 || (uint32_t)decoded_bytes != source_size) {
    return ARNM_ERROR_DECODE_FAILED;
  }
  return ARNM_SUCCESS;
}
