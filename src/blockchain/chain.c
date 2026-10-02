#include "gradido_blockchain_core/blockchain/chain.h"

#include <string.h>

/**
 * A chain is a row of segments over one gapless run of transaction numbers. Every question is
 * asked of each segment and the answers are put back together here -- which is the whole of
 * this file, and the reason the row exists even when it holds a single volume.
 */

/** Segments per bucket: eight. An ordinary chain pays one bucket for its whole row. */
#define CHAIN_SEGMENTS_BUCKET_LOG2 3u

static grdb_transactions *segment_at(const grdb_chain *chain, uint32_t position) {
  return (grdb_transactions *)arnm_bvec_get(&chain->segments, position);
}

/** Which segment a number falls in, counted from zero and not from the chain's first. */
static uint64_t segment_of(const grdb_chain *chain, uint64_t tx_nr) {
  return tx_nr / (uint64_t)chain->segment_transactions;
}

// ********** building *******************

arnm_result grdb_chain_init(grdb_chain *chain, const grdb_chain_options *options, arnm *source) {
  if (!chain) { return ARNM_ERROR_NULL_POINTER; }
  const grdb_chain_options empty = {0};
  if (!options) { options = &empty; }

  memset(chain, 0, sizeof(*chain));
  chain->source = source;
  chain->segment_transactions = options->segment_transactions ? options->segment_transactions
                                                              : GRDB_SEGMENT_TRANSACTIONS_DEFAULT;
  chain->expected_addresses = options->expected_addresses;
  chain->expected_days = options->expected_days;
  if (options->store) { chain->store = *options->store; }

  arnm_result result = arnm_bvec_init(
      &chain->segments, CHAIN_SEGMENTS_BUCKET_LOG2, 0, sizeof(grdb_transactions), source
  );
  if (ARNM_SUCCESS != result) {
    memset(chain, 0, sizeof(*chain));
    return result;
  }

  const grdb_addresses_options address_options = {
      .expected_addresses = options->expected_addresses
  };
  result = grdb_addresses_init(&chain->addresses, &address_options, source);
  if (ARNM_SUCCESS != result) {
    arnm_bvec_free(&chain->segments);
    memset(chain, 0, sizeof(*chain));
    return result;
  }

  chain->ready = true;
  return ARNM_SUCCESS;
}

/** Every segment released and the row emptied; the row itself stays allocated. */
static void release_segments(grdb_chain *chain) {
  const uint32_t count = arnm_bvec_size(&chain->segments);
  for (uint32_t i = 0; i < count; ++i) { grdb_transactions_release(segment_at(chain, i)); }
  arnm_bvec_clear(&chain->segments);
}

void grdb_chain_release(grdb_chain *chain) {
  if (!chain || !chain->ready) { return; }
  release_segments(chain);
  arnm_bvec_free(&chain->segments);
  grdb_addresses_release(&chain->addresses);
  memset(chain, 0, sizeof(*chain));
}

void grdb_chain_reset(grdb_chain *chain) {
  if (!chain || !chain->ready) { return; }
  // the row goes: a fresh fill may begin at another number, and a segment's base is fixed when
  // it is opened. The register keeps its tables, which a fresh fill can use unchanged.
  release_segments(chain);
  grdb_addresses_reset(&chain->addresses);
  chain->first_segment = 0;
  chain->min_tx_nr = 0;
  chain->max_tx_nr = 0;
  chain->transaction_count = 0;
}

/** Opens the segment that @p number falls in, at the end of the row. */
static arnm_result open_segment(grdb_chain *chain, uint64_t number, grdb_transactions **out) {
  void *slot = NULL;
  arnm_result result = arnm_bvec_emplace(&chain->segments, &slot);
  if (ARNM_SUCCESS != result) { return result; }

  const grdb_transactions_options options = {
      .base_tx_nr = segment_of(chain, number) * (uint64_t)chain->segment_transactions,
      .expected_addresses = chain->expected_addresses,
      .expected_days = chain->expected_days
  };
  result = grdb_transactions_init((grdb_transactions *)slot, &options, chain->source);
  if (ARNM_SUCCESS != result) {
    // init leaves the slot zeroed when it fails, so giving it back is enough
    arnm_bvec_pop(&chain->segments);
    return result;
  }
  *out = (grdb_transactions *)slot;
  return ARNM_SUCCESS;
}

arnm_result grdb_chain_add(grdb_chain *chain, const grdr_complete_transaction *tx) {
  if (!chain || !tx) { return ARNM_ERROR_NULL_POINTER; }
  if (!chain->ready) { return ARNM_ERROR_INVALID_STATE; }
  if (!tx->tx_nr) { return ARNM_ERROR_INVALID_PARAM; }
  // a chain numbers a transaction as it completes it, one above the one before
  if (chain->transaction_count && tx->tx_nr != chain->max_tx_nr + 1u) {
    return ARNM_ERROR_INVALID_PARAM;
  }

  const uint64_t number = segment_of(chain, tx->tx_nr);
  if (!chain->transaction_count && !arnm_bvec_size(&chain->segments)) {
    chain->first_segment = number;
  }
  if (number < chain->first_segment) { return ARNM_ERROR_INVALID_PARAM; }
  const uint64_t position = number - chain->first_segment;
  if (position > (uint64_t)arnm_bvec_size(&chain->segments)) {
    // gapless numbers cannot skip a segment; a number that does is not this chain's next
    return ARNM_ERROR_INVALID_PARAM;
  }

  grdb_transactions *segment = NULL;
  if (position == (uint64_t)arnm_bvec_size(&chain->segments)) {
    const arnm_result opened = open_segment(chain, tx->tx_nr, &segment);
    if (ARNM_SUCCESS != opened) { return opened; }
  } else {
    segment = segment_at(chain, (uint32_t)position);
  }

  // A previous add that reached the segment and then failed in the register left this number in
  // the segment already. It is recognisable without a flag: only a half added transaction can
  // be the segment's largest while the chain has not counted it.
  const bool in_segment = segment->transaction_count && segment->max_tx_nr == tx->tx_nr;
  if (!in_segment) {
    const arnm_result result = grdb_transactions_add(segment, tx);
    if (ARNM_SUCCESS != result) { return result; }
  }

  const arnm_result result = grdb_addresses_add(&chain->addresses, tx);
  if (ARNM_SUCCESS != result) { return result; }

  if (!chain->transaction_count) { chain->min_tx_nr = tx->tx_nr; }
  chain->max_tx_nr = tx->tx_nr;
  ++chain->transaction_count;
  return ARNM_SUCCESS;
}

// ********** the transactions themselves *******************

arnm_result grdb_chain_fetch(
    const grdb_chain *chain, uint64_t tx_nr, const grdr_complete_transaction **out
) {
  if (!out) { return ARNM_ERROR_NULL_POINTER; }
  if (!chain || !chain->ready) { return ARNM_ERROR_INVALID_STATE; }
  return grdb_chain_store_fetch(&chain->store, tx_nr, out);
}

arnm_result grdb_chain_load(
    grdb_chain *chain, uint64_t from_tx_nr, uint64_t to_tx_nr, uint64_t *loaded
) {
  if (loaded) { *loaded = 0; }
  if (!chain || !chain->ready) { return ARNM_ERROR_INVALID_STATE; }
  if (!grdb_chain_store_is_readable(&chain->store)) { return ARNM_ERROR_INVALID_STATE; }
  if (!from_tx_nr) { from_tx_nr = 1u; }
  if (to_tx_nr && to_tx_nr < from_tx_nr) { return ARNM_ERROR_INVALID_PARAM; }

  uint64_t count = 0;
  arnm_result result = ARNM_SUCCESS;

  for (uint64_t number = from_tx_nr; !to_tx_nr || number <= to_tx_nr; ++number) {
    const grdr_complete_transaction *tx = NULL;
    result = grdb_chain_store_fetch(&chain->store, number, &tx);
    if (ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS == result && !to_tx_nr) {
      // the store ran out and the caller named no end: that is where the chain stops
      result = ARNM_SUCCESS;
      break;
    }
    if (ARNM_SUCCESS != result) { break; }

    // add reads the transaction and keeps nothing of it, so the store may reuse the place it
    // was handed from as soon as the next number is asked for
    result = grdb_chain_add(chain, tx);
    if (ARNM_SUCCESS != result) { break; }
    ++count;
  }

  if (loaded) { *loaded = count; }
  return result;
}

// ********** asking *******************

arnm_result grdb_chain_count(
    const grdb_chain *chain, const grdb_transactions_filter *filter, uint64_t *out
) {
  if (!chain || !filter || !out) { return ARNM_ERROR_NULL_POINTER; }
  if (!chain->ready) { return ARNM_ERROR_INVALID_STATE; }

  const uint32_t segments = arnm_bvec_size(&chain->segments);
  uint64_t total = 0;
  for (uint32_t i = 0; i < segments; ++i) {
    uint64_t here = 0;
    const arnm_result result = grdb_transactions_count(segment_at(chain, i), filter, &here);
    if (ARNM_SUCCESS != result) { return result; }
    total += here;
  }
  *out = total;
  return ARNM_SUCCESS;
}

arnm_result grdb_chain_listing(
    const grdb_chain *chain,
    const grdb_transactions_filter *filter,
    uint32_t skip,
    uint32_t size,
    bool descending,
    uint64_t *out,
    uint32_t *written,
    uint64_t *count
) {
  if (!chain || !filter || !written || !count || (size && !out)) { return ARNM_ERROR_NULL_POINTER; }
  if (!chain->ready) { return ARNM_ERROR_INVALID_STATE; }
  if (size > GRDB_PAGE_MAX) { return ARNM_ERROR_INVALID_PARAM; }

  const uint32_t segments = arnm_bvec_size(&chain->segments);
  uint32_t remaining_skip = skip;
  uint32_t remaining_size = size;
  uint32_t taken = 0;
  uint64_t matches = 0;

  for (uint32_t i = 0; i < segments; ++i) {
    // the page runs from the end it starts at, so the row is walked from that end too
    const uint32_t position = descending ? segments - 1u - i : i;
    uint32_t here_written = 0;
    uint64_t here_count = 0;
    const arnm_result result = grdb_transactions_listing(
        segment_at(chain, position), filter, remaining_skip, remaining_size, descending,
        remaining_size ? out + taken : NULL, &here_written, &here_count
    );
    if (ARNM_SUCCESS != result) { return result; }

    // the count answers for the filter and not for the page, so every segment is counted even
    // after the page is full -- with a size of zero, which reads no value
    matches += here_count;
    taken += here_written;
    remaining_size -= here_written;
    if ((uint64_t)remaining_skip <= here_count) {
      remaining_skip = 0;
    } else {
      remaining_skip -= (uint32_t)here_count;
    }
  }

  *written = taken;
  *count = matches;
  return ARNM_SUCCESS;
}

bool grdb_chain_newest(
    const grdb_chain *chain, const grdb_transactions_filter *filter, uint64_t *out
) {
  if (!chain || !filter || !out || !chain->ready) { return false; }
  const uint32_t segments = arnm_bvec_size(&chain->segments);
  for (uint32_t i = segments; i > 0; --i) {
    if (grdb_transactions_newest(segment_at(chain, i - 1u), filter, out)) { return true; }
  }
  return false;
}

bool grdb_chain_range_of_days(
    const grdb_chain *chain, int64_t from_second, int64_t to_second, uint64_t *min, uint64_t *max
) {
  if (!chain || !chain->ready || !min || !max) { return false; }
  const uint32_t segments = arnm_bvec_size(&chain->segments);
  bool found = false;
  uint64_t lowest = 0;
  uint64_t highest = 0;

  for (uint32_t i = 0; i < segments; ++i) {
    uint64_t here_min = 0;
    uint64_t here_max = 0;
    if (!grdb_transactions_range_of_days(
            segment_at(chain, i), from_second, to_second, &here_min, &here_max
        )) {
      continue;
    }
    // the segments' spans lie end to end, so the outermost two are the chain's span
    if (!found || here_min < lowest) { lowest = here_min; }
    if (!found || here_max > highest) { highest = here_max; }
    found = true;
  }

  if (!found) { return false; }
  *min = lowest;
  *max = highest;
  return true;
}

// ********** what the chain says about an address *******************

grdt_address grdb_chain_address_type(const grdb_chain *chain, const uint8_t *public_key) {
  return chain ? grdb_addresses_type(&chain->addresses, public_key) : GRDT_ADDRESS_NONE;
}

bool grdb_chain_address_type_at(
    const grdb_chain *chain, const uint8_t *public_key, uint64_t at_tx_nr, grdt_address *out
) {
  return chain && grdb_addresses_type_at(&chain->addresses, public_key, at_tx_nr, out);
}

bool grdb_chain_address_last_balance(
    const grdb_chain *chain, const uint8_t *public_key, uint64_t *out
) {
  return chain && grdb_addresses_last_balance(&chain->addresses, public_key, out);
}

bool grdb_chain_knows_address(const grdb_chain *chain, const uint8_t *public_key) {
  return chain && grdb_addresses_knows(&chain->addresses, public_key);
}

// ********** reading what is in it *******************

uint32_t grdb_chain_segment_count(const grdb_chain *chain) {
  return chain && chain->ready ? arnm_bvec_size(&chain->segments) : 0u;
}

const grdb_transactions *grdb_chain_segment(const grdb_chain *chain, uint32_t position) {
  if (!chain || !chain->ready || position >= arnm_bvec_size(&chain->segments)) { return NULL; }
  return segment_at(chain, position);
}
