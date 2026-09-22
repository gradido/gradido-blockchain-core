#include "gradido_blockchain_core/blockchain/store.h"

arnm_result grdb_chain_store_fetch(
    const grdb_chain_store *store, uint64_t tx_nr, const grdr_complete_transaction **out
) {
  if (!out) { return ARNM_ERROR_NULL_POINTER; }
  if (!grdb_chain_store_is_readable(store)) { return ARNM_ERROR_INVALID_STATE; }
  return store->fetch(store->user_data, tx_nr, out);
}

arnm_result grdb_chain_store_append(
    const grdb_chain_store *store,
    uint64_t tx_nr,
    grdr_complete_transaction *tx,
    const arnm_memory_block *serialized
) {
  if (!tx && (!serialized || !serialized->data || !serialized->size)) {
    return ARNM_ERROR_NULL_POINTER;
  }
  if (!grdb_chain_store_is_writable(store)) { return ARNM_ERROR_INVALID_STATE; }
  return store->append(store->user_data, tx_nr, tx, serialized);
}
