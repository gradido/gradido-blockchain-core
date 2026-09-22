#include "gradido_blockchain_core/blockchain/in_memory.h"

#include <string.h>

/** Transactions per bucket: 64. */
#define IN_MEMORY_RECORDS_BUCKET_LOG2 6u

static grdr_complete_transaction *record_at(const grdb_in_memory *store, uint32_t position) {
  return (grdr_complete_transaction *)arnm_bvec_get(&store->records, position);
}

/** Releases every transaction the store took, and empties the vector. */
static void free_records(grdb_in_memory *store) {
  const uint32_t count = arnm_bvec_size(&store->records);
  for (uint32_t i = 0; i < count; ++i) { grdr_complete_transaction_release(record_at(store, i)); }
  arnm_bvec_clear(&store->records);
}

arnm_result grdb_in_memory_init(
    grdb_in_memory *store,
    const grdb_in_memory_options *options,
    const uint8_t community_uuid[ARNM_UUID_BINARY_SIZE],
    arnm *source
) {
  if (!store || !community_uuid) { return ARNM_ERROR_NULL_POINTER; }
  const grdb_in_memory_options empty = {0};
  if (!options) { options = &empty; }

  memset(store, 0, sizeof(*store));
  store->source = source;
  memcpy(store->community_uuid, community_uuid, ARNM_UUID_BINARY_SIZE);

  const uint32_t wanted =
      options->scratch_bytes ? options->scratch_bytes : GRDB_IN_MEMORY_SCRATCH_DEFAULT;
  // a decode borrows the scratch as an arena, which wants a multiple of eight
  store->scratch_size = (wanted + 7u) & ~7u;

  arnm_result result = arnm_bvec_init(
      &store->records, IN_MEMORY_RECORDS_BUCKET_LOG2, 0, sizeof(grdr_complete_transaction), source
  );
  if (ARNM_SUCCESS != result) {
    memset(store, 0, sizeof(*store));
    return result;
  }
  if (options->expected_transactions) {
    result = arnm_bvec_reserve(&store->records, options->expected_transactions);
    if (ARNM_SUCCESS != result) {
      arnm_bvec_free(&store->records);
      memset(store, 0, sizeof(*store));
      return result;
    }
  }

  result = arnm_alloc(&store->scratch, store->scratch_size, source);
  if (ARNM_SUCCESS != result) {
    arnm_bvec_free(&store->records);
    memset(store, 0, sizeof(*store));
    return result;
  }

  store->ready = true;
  return ARNM_SUCCESS;
}

void grdb_in_memory_release(grdb_in_memory *store) {
  if (!store || !store->ready) { return; }
  free_records(store);
  arnm_bvec_free(&store->records);
  if (store->scratch) { arnm_free(store->scratch, store->scratch_size, store->source); }
  memset(store, 0, sizeof(*store));
}

void grdb_in_memory_reset(grdb_in_memory *store) {
  if (!store || !store->ready) { return; }
  free_records(store);
  store->first_tx_nr = 0;
  store->max_tx_nr = 0;
}

// ********** the two calls a chain makes *******************

/** Where @p tx_nr sits in the vector, or false when the store does not hold it. */
static bool position_of(const grdb_in_memory *store, uint64_t tx_nr, uint32_t *out) {
  if (!store->first_tx_nr || tx_nr < store->first_tx_nr || tx_nr > store->max_tx_nr) {
    return false;
  }
  *out = (uint32_t)(tx_nr - store->first_tx_nr);
  return true;
}

static arnm_result in_memory_fetch(
    void *user_data, uint64_t tx_nr, const grdr_complete_transaction **out
) {
  const grdb_in_memory *store = (const grdb_in_memory *)user_data;
  if (!store || !store->ready || !out) { return ARNM_ERROR_NULL_POINTER; }

  uint32_t position = 0;
  if (!position_of(store, tx_nr, &position)) { return ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS; }
  // the address of what the store holds: no decode, no copy, and it stays valid while it holds it
  *out = record_at(store, position);
  return ARNM_SUCCESS;
}

static arnm_result in_memory_append(
    void *user_data,
    uint64_t tx_nr,
    grdr_complete_transaction *tx,
    const arnm_memory_block *serialized
) {
  grdb_in_memory *store = (grdb_in_memory *)user_data;
  const bool has_bytes = serialized && serialized->data && serialized->size;
  if (!store || !store->ready || (!tx && !has_bytes)) { return ARNM_ERROR_NULL_POINTER; }
  if (!tx_nr) { return ARNM_ERROR_INVALID_PARAM; }
  // a store keeps a run of numbers without gaps, the way a chain hands them over
  if (store->first_tx_nr && tx_nr != store->max_tx_nr + 1u) { return ARNM_ERROR_INVALID_PARAM; }

  void *slot = NULL;
  arnm_result result = arnm_bvec_emplace(&store->records, &slot);
  if (ARNM_SUCCESS != result) { return result; }
  grdr_complete_transaction *kept = (grdr_complete_transaction *)slot;

  if (has_bytes) {
    // bytes rather than an object: decoded once here, never again
    grdr_complete_transaction_init(kept);
    result = grdr_complete_transaction_init_from_protobuf(
        kept, serialized->data, serialized->size, store->community_uuid, store->scratch,
        store->scratch_size
    );
    if (ARNM_SUCCESS != result) {
      arnm_bvec_pop(&store->records);
      return result;
    }
    // a caller that handed over both is left empty all the same
    if (tx) { grdr_complete_transaction_release(tx); }
  } else {
    // the move: the struct travels, its arena stays where it is, and the caller is left empty
    *kept = *tx;
    grdr_complete_transaction_init(tx);
  }

  if (!store->first_tx_nr) { store->first_tx_nr = tx_nr; }
  store->max_tx_nr = tx_nr;
  return ARNM_SUCCESS;
}

grdb_chain_store grdb_in_memory_as_store(grdb_in_memory *store) {
  grdb_chain_store wrapped;
  memset(&wrapped, 0, sizeof(wrapped));
  if (!store || !store->ready) { return wrapped; }
  wrapped.user_data = store;
  wrapped.fetch = in_memory_fetch;
  wrapped.append = in_memory_append;
  return wrapped;
}

// ********** reading what is in it *******************

uint64_t grdb_in_memory_size(const grdb_in_memory *store) {
  if (!store || !store->ready || !store->first_tx_nr) { return 0u; }
  return store->max_tx_nr - store->first_tx_nr + 1u;
}

const grdr_complete_transaction *grdb_in_memory_at(const grdb_in_memory *store, uint64_t tx_nr) {
  const grdr_complete_transaction *found = NULL;
  if (!store) { return NULL; }
  return ARNM_SUCCESS == in_memory_fetch((void *)(uintptr_t)store, tx_nr, &found) ? found : NULL;
}
