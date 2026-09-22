#include "gradido_blockchain_core/blockchain/ffi.h"

#include <string.h>

arnm_result grdb_chain_ffi_init(
    grdb_chain_ffi *adapter,
    const grdb_chain_ffi_host *host,
    const grdb_chain_ffi_options *options,
    const uint8_t community_uuid[ARNM_UUID_BINARY_SIZE],
    arnm *source
) {
  if (!adapter || !host || !host->get || !community_uuid) { return ARNM_ERROR_NULL_POINTER; }
  const grdb_chain_ffi_options empty = {0};
  if (!options) { options = &empty; }

  const uint32_t wanted =
      options->scratch_bytes ? options->scratch_bytes : GRDB_FFI_SCRATCH_DEFAULT;
  // rounded up to the multiple of eight the decoder's arena wants -- which past this bound would
  // wrap to a scratch of nothing, refused only at the first decode, far from its cause
  if (wanted > UINT32_MAX - 7u) { return ARNM_ERROR_ARITHMETIC_OVERFLOW; }

  memset(adapter, 0, sizeof(*adapter));
  grdr_complete_transaction_init(&adapter->held);
  adapter->host = *host;
  adapter->source = source;
  memcpy(adapter->community_uuid, community_uuid, ARNM_UUID_BINARY_SIZE);
  adapter->scratch_size = (wanted + 7u) & ~7u;

  const arnm_result result = arnm_alloc(&adapter->scratch, adapter->scratch_size, source);
  if (ARNM_SUCCESS != result) {
    memset(adapter, 0, sizeof(*adapter));
    return result;
  }

  adapter->ready = true;
  return ARNM_SUCCESS;
}

void grdb_chain_ffi_release(grdb_chain_ffi *adapter) {
  if (!adapter || !adapter->ready) { return; }
  grdr_complete_transaction_release(&adapter->held);
  if (adapter->scratch) { arnm_free(adapter->scratch, adapter->scratch_size, adapter->source); }
  memset(adapter, 0, sizeof(*adapter));
}

// ********** the two calls a chain makes *******************

static arnm_result ffi_fetch(
    void *user_data, uint64_t tx_nr, const grdr_complete_transaction **out
) {
  grdb_chain_ffi *adapter = (grdb_chain_ffi *)user_data;
  if (!adapter || !adapter->ready || !out) { return ARNM_ERROR_NULL_POINTER; }

  // already decoded, from the fetch or the append before this one
  if (adapter->held_tx_nr && adapter->held_tx_nr == tx_nr) {
    *out = &adapter->held;
    return ARNM_SUCCESS;
  }

  const uint8_t *data = NULL;
  uint32_t size = 0;
  const int answer = adapter->host.get(adapter->host.user_data, tx_nr, &data, &size);
  if (GRDB_FFI_NOT_FOUND == answer) { return ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS; }
  if (GRDB_FFI_OK != answer) { return ARNM_ERROR_DECODE_FAILED; }
  if (!data || !size) {
    // the host said yes and handed over nothing; tell it we are done, then refuse
    if (adapter->host.release) { adapter->host.release(adapter->host.user_data, data, size); }
    return ARNM_ERROR_DECODE_FAILED;
  }

  adapter->held_tx_nr = 0;
  const arnm_result result = grdr_complete_transaction_init_from_protobuf(
      &adapter->held, data, size, adapter->community_uuid, adapter->scratch, adapter->scratch_size
  );
  // the host hears once per handover, decoded or not: it may be holding a row open
  if (adapter->host.release) { adapter->host.release(adapter->host.user_data, data, size); }
  if (ARNM_SUCCESS != result) { return result; }

  adapter->held_tx_nr = tx_nr;
  *out = &adapter->held;
  return ARNM_SUCCESS;
}

static arnm_result ffi_append_serialized(
    void *user_data, uint64_t tx_nr, const arnm_memory_block *serialized
) {
  grdb_chain_ffi *adapter = (grdb_chain_ffi *)user_data;
  if (!adapter || !adapter->ready || !serialized) { return ARNM_ERROR_NULL_POINTER; }
  if (!adapter->host.put) { return ARNM_ERROR_INVALID_STATE; }
  if (!serialized->data || !serialized->size) { return ARNM_ERROR_INVALID_PARAM; }

  const int answer =
      adapter->host.put(adapter->host.user_data, tx_nr, serialized->data, serialized->size);
  return GRDB_FFI_OK == answer ? ARNM_SUCCESS : ARNM_ERROR_ENCODE_FAILED;
}

grdb_chain_store grdb_chain_ffi_as_store(grdb_chain_ffi *adapter) {
  grdb_chain_store wrapped;
  memset(&wrapped, 0, sizeof(wrapped));
  if (!adapter || !adapter->ready) { return wrapped; }
  wrapped.user_data = adapter;
  wrapped.fetch = ffi_fetch;
  // the host keeps bytes, so bytes are the one form it takes -- and without a put, none at all
  wrapped.append_serialized = adapter->host.put ? ffi_append_serialized : NULL;
  return wrapped;
}
