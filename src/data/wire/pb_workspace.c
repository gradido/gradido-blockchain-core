#include "gradido_blockchain_core/data/wire/pb_workspace.h"

#include "pbtools.h"

#include <stdint.h>

hostmem_result grdw_pb_workspace_check(const hostmem_memory_block *workspace) {
  if (!workspace || !workspace->data) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!workspace->size) { return HOSTMEM_ERROR_INVALID_PARAM; }
  // pbtools lays its heap out from the first byte and stores aligned members in it; a stretch
  // that starts off alignment would have every one of them misaligned. Every hostmem allocation
  // already satisfies this, so the check costs a caller nothing that was not already true.
  if ((uintptr_t)workspace->data & 7u) { return HOSTMEM_ERROR_INVALID_PARAM; }
  return HOSTMEM_SUCCESS;
}

hostmem_result grdw_pb_decode_verdict(int decoded_bytes, uint32_t source_size) {
  if (PBTOOLS_OUT_OF_MEMORY == -decoded_bytes) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  // a decode that stopped early or ran past the end read a message that contradicts its length
  if (decoded_bytes < 0 || (uint32_t)decoded_bytes != source_size) {
    return HOSTMEM_ERROR_DECODE_FAILED;
  }
  return HOSTMEM_SUCCESS;
}

hostmem_result grdw_pb_encode_verdict(int *final_size, int encoded_bytes) {
  if (PBTOOLS_OUT_OF_MEMORY == -encoded_bytes) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  if (PBTOOLS_ENCODE_BUFFER_FULL == -encoded_bytes) {
    return HOSTMEM_ERROR_DESTINATION_BUFFER_TO_SMALL;
  }
  if (encoded_bytes < 0) { return HOSTMEM_ERROR_ENCODE_FAILED; }
  if (final_size) { *final_size = encoded_bytes; }
  return HOSTMEM_SUCCESS;
}
