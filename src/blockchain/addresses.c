#include "gradido_blockchain_core/blockchain/addresses.h"

#include <string.h>

/**
 * What a chain decides about an address, kept so that deciding it again costs nothing. A type
 * is given by few transactions and read by many, and a balance change is a single number that
 * only ever moves forward -- so both are written once where they happen and read where they
 * are needed.
 */

/** Keys per bucket of the key vector: 1024 keys of 32 bytes. */
#define ADDRESSES_KEYS_BUCKET_LOG2 10u
/** Entries per bucket: 4096 of 16 bytes. */
#define ADDRESSES_ENTRIES_BUCKET_LOG2 12u
/** Type records per bucket: 4096 of 16 bytes. Most addresses have one, so this is generous. */
#define ADDRESSES_CHANGES_BUCKET_LOG2 12u
/** No record: an address that has a balance but was never given a type. */
#define ADDRESSES_NO_CHANGE UINT32_MAX

/** One address: where its type records start, and when its balance last moved. */
typedef struct address_entry {
  uint64_t last_balance_tx; /**< Largest transaction with a balance entry; 0 for none. */
  uint32_t last_change;     /**< Newest type record, or @ref ADDRESSES_NO_CHANGE. */
} address_entry;

/** One transaction that gave an address a type, linked to the one before it. */
typedef struct type_change {
  uint64_t tx_nr;    /**< The transaction that gave the type. */
  uint32_t previous; /**< The record before it, or @ref ADDRESSES_NO_CHANGE. */
  uint8_t type;      /**< @ref grdt_address. */
} type_change;

static address_entry *entry_at(const grdb_addresses *index, uint32_t id) {
  return (address_entry *)arnm_bvec_get(&index->entries, id);
}

static const type_change *change_at(const grdb_addresses *index, uint32_t position) {
  return (const type_change *)arnm_bvec_get(&index->type_changes, position);
}

/** The entry of @p key, opened on first sight of it. */
static arnm_result entry_for_key(grdb_addresses *index, const uint8_t *key, address_entry **out) {
  uint32_t id = 0;
  bool inserted = false;
  const arnm_result result = arnm_key_map_get_or_insert(&index->keys, key, &id, &inserted);
  if (ARNM_SUCCESS != result) { return result; }
  (void)inserted;
  // Grown up to the id, not by one. A key the map took while the vector found no room for its
  // entry stays in the map; asked for again, it is not new, and a vector grown only for new keys
  // would never reach it -- every later transaction naming it would be refused for good, room or
  // not. Ids are handed out densely, so what is missing is only ever that last stretch.
  while (arnm_bvec_size(&index->entries) <= id) {
    void *slot = NULL;
    const arnm_result grown = arnm_bvec_emplace(&index->entries, &slot);
    if (ARNM_SUCCESS != grown) { return grown; }
    address_entry *entry = (address_entry *)slot;
    entry->last_balance_tx = 0;
    entry->last_change = ADDRESSES_NO_CHANGE;
  }
  *out = entry_at(index, id);
  return ARNM_SUCCESS;
}

/** The entry of @p key, or NULL when the index never saw it. */
static const address_entry *entry_of_key(const grdb_addresses *index, const uint8_t *public_key) {
  uint32_t id = 0;
  if (!index || !index->ready || !public_key) { return NULL; }
  if (!arnm_key_map_find(&index->keys, public_key, &id)) { return NULL; }
  if (id >= arnm_bvec_size(&index->entries)) { return NULL; }
  return entry_at(index, id);
}

/** Writes one type record in front of the address's chain of them. */
static arnm_result note_type(
    grdb_addresses *index, const uint8_t *key, uint64_t tx_nr, grdt_address type
) {
  if (!key) { return ARNM_SUCCESS; }
  bool zero = true;
  for (uint32_t i = 0; i < SIGN_PUBLIC_KEY_SIZE && zero; ++i) { zero = !key[i]; }
  if (zero) { return ARNM_SUCCESS; }
  if ((uint32_t)type > (uint32_t)GRDT_ADDRESS_DEFERRED_TRANSFER) {
    return ARNM_ERROR_INVALID_ENUM_TYPE;
  }

  address_entry *entry = NULL;
  const arnm_result result = entry_for_key(index, key, &entry);
  if (ARNM_SUCCESS != result) { return result; }

  // An add that was refused part way leaves the records it had already written, and adding the
  // transaction again comes back here. Numbers only grow, so this transaction's records are the
  // newest of the key: one with this number and this type is this very record, written before,
  // and a second would list the transaction twice among the key's type changes.
  for (uint32_t at = entry->last_change; ADDRESSES_NO_CHANGE != at;) {
    const type_change *known = change_at(index, at);
    if (known->tx_nr != tx_nr) { break; }
    if (known->type == (uint8_t)type) { return ARNM_SUCCESS; }
    at = known->previous;
  }

  void *slot = NULL;
  const arnm_result grown = arnm_bvec_emplace(&index->type_changes, &slot);
  if (ARNM_SUCCESS != grown) { return grown; }
  type_change *change = (type_change *)slot;
  change->tx_nr = tx_nr;
  change->previous = entry->last_change;
  change->type = (uint8_t)type;
  entry->last_change = arnm_bvec_size(&index->type_changes) - 1u;
  return ARNM_SUCCESS;
}

// ********** building *******************

arnm_result grdb_addresses_init(
    grdb_addresses *index, const grdb_addresses_options *options, arnm *source
) {
  if (!index) { return ARNM_ERROR_NULL_POINTER; }
  const grdb_addresses_options empty = {0};
  if (!options) { options = &empty; }

  memset(index, 0, sizeof(*index));
  index->source = source;

  arnm_result result =
      arnm_key_map_init(&index->keys, SIGN_PUBLIC_KEY_SIZE, ADDRESSES_KEYS_BUCKET_LOG2, source);
  if (ARNM_SUCCESS != result) { return result; }
  result = arnm_bvec_init(
      &index->entries, ADDRESSES_ENTRIES_BUCKET_LOG2, 0, sizeof(address_entry), source
  );
  if (ARNM_SUCCESS != result) { goto fail_map; }
  result = arnm_bvec_init(
      &index->type_changes, ADDRESSES_CHANGES_BUCKET_LOG2, 0, sizeof(type_change), source
  );
  if (ARNM_SUCCESS != result) { goto fail_entries; }

  if (options->expected_addresses) {
    result = arnm_key_map_reserve(&index->keys, options->expected_addresses);
    if (ARNM_SUCCESS == result) {
      result = arnm_bvec_reserve(&index->entries, options->expected_addresses);
    }
    if (ARNM_SUCCESS != result) { goto fail_changes; }
  }

  index->ready = true;
  return ARNM_SUCCESS;

fail_changes:
  arnm_bvec_free(&index->type_changes);
fail_entries:
  arnm_bvec_free(&index->entries);
fail_map:
  arnm_key_map_free(&index->keys);
  memset(index, 0, sizeof(*index));
  return result;
}

void grdb_addresses_release(grdb_addresses *index) {
  if (!index || !index->ready) { return; }
  arnm_bvec_free(&index->type_changes);
  arnm_bvec_free(&index->entries);
  arnm_key_map_free(&index->keys);
  memset(index, 0, sizeof(*index));
}

void grdb_addresses_reset(grdb_addresses *index) {
  if (!index || !index->ready) { return; }
  arnm_key_map_clear(&index->keys);
  arnm_bvec_clear(&index->entries);
  arnm_bvec_clear(&index->type_changes);
  index->max_tx_nr = 0;
  index->transaction_count = 0;
}

arnm_result grdb_addresses_add(grdb_addresses *index, const grdr_complete_transaction *tx) {
  if (!index || !tx) { return ARNM_ERROR_NULL_POINTER; }
  if (!index->ready) { return ARNM_ERROR_INVALID_STATE; }
  if (index->transaction_count && tx->tx_nr <= index->max_tx_nr) {
    return ARNM_ERROR_INVALID_PARAM;
  }
  if ((uint32_t)tx->transaction_type >= GRDT_TRANSACTION_COUNT) {
    return ARNM_ERROR_INVALID_ENUM_TYPE;
  }

  arnm_result result = ARNM_SUCCESS;
  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    // the two community accounts are typed by the root itself; the root key keeps no type
    result =
        note_type(index, tx->community_root.gmw_public_key, tx->tx_nr, GRDT_ADDRESS_COMMUNITY_GMW);
    if (ARNM_SUCCESS != result) { return result; }
    result =
        note_type(index, tx->community_root.auf_public_key, tx->tx_nr, GRDT_ADDRESS_COMMUNITY_AUF);
    if (ARNM_SUCCESS != result) { return result; }
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    // both keys of a registration carry the type it names
    result = note_type(index, tx->register_address.user_public_key, tx->tx_nr, tx->address_type);
    if (ARNM_SUCCESS != result) { return result; }
    result = note_type(index, tx->register_address.account_public_key, tx->tx_nr, tx->address_type);
    if (ARNM_SUCCESS != result) { return result; }
    break;
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
    // the one type an address receives without ever being registered
    result =
        note_type(index, tx->transfer.recipient_pubkey, tx->tx_nr, GRDT_ADDRESS_DEFERRED_TRANSFER);
    if (ARNM_SUCCESS != result) { return result; }
    break;
  default:
    break;
  }

  for (size_t i = 0; i < tx->account_balances_count; ++i) {
    const uint8_t *key = tx->account_balances[i].pubkey;
    bool zero = true;
    for (uint32_t b = 0; b < SIGN_PUBLIC_KEY_SIZE && zero; ++b) { zero = !key[b]; }
    if (zero) { continue; }
    address_entry *entry = NULL;
    result = entry_for_key(index, key, &entry);
    if (ARNM_SUCCESS != result) { return result; }
    // numbers only ever grow, so the newest is the last one written
    entry->last_balance_tx = tx->tx_nr;
  }

  index->max_tx_nr = tx->tx_nr;
  ++index->transaction_count;
  return ARNM_SUCCESS;
}

// ********** asking *******************

grdt_address grdb_addresses_type(const grdb_addresses *index, const uint8_t *public_key) {
  const address_entry *entry = entry_of_key(index, public_key);
  if (!entry || ADDRESSES_NO_CHANGE == entry->last_change) { return GRDT_ADDRESS_NONE; }
  return (grdt_address)change_at(index, entry->last_change)->type;
}

bool grdb_addresses_type_at(
    const grdb_addresses *index, const uint8_t *public_key, uint64_t at_tx_nr, grdt_address *out
) {
  const address_entry *entry = entry_of_key(index, public_key);
  if (!entry || !out) { return false; }
  // newest first: the first record at or below the number is the one that was standing then
  for (uint32_t position = entry->last_change; ADDRESSES_NO_CHANGE != position;) {
    const type_change *change = change_at(index, position);
    if (change->tx_nr <= at_tx_nr) {
      *out = (grdt_address)change->type;
      return true;
    }
    position = change->previous;
  }
  return false;
}

uint32_t grdb_addresses_type_changes(
    const grdb_addresses *index, const uint8_t *public_key, uint64_t *out, uint32_t size
) {
  const address_entry *entry = entry_of_key(index, public_key);
  if (!entry || (size && !out)) { return 0u; }
  uint32_t written = 0;
  for (uint32_t position = entry->last_change; ADDRESSES_NO_CHANGE != position && written < size;) {
    const type_change *change = change_at(index, position);
    out[written++] = change->tx_nr;
    position = change->previous;
  }
  return written;
}

bool grdb_addresses_last_balance(
    const grdb_addresses *index, const uint8_t *public_key, uint64_t *out
) {
  const address_entry *entry = entry_of_key(index, public_key);
  if (!entry || !out || !entry->last_balance_tx) { return false; }
  *out = entry->last_balance_tx;
  return true;
}

bool grdb_addresses_knows(const grdb_addresses *index, const uint8_t *public_key) {
  return entry_of_key(index, public_key) != NULL;
}

uint32_t grdb_addresses_size(const grdb_addresses *index) {
  return index && index->ready ? arnm_key_map_size(&index->keys) : 0u;
}
