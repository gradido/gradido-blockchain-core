#include "gradido_blockchain_core/blockchain/transactions.h"

#include <string.h>

/**
 * Sets of transaction numbers, one chain's worth, and the walk that turns a filter into an
 * answer. What a query costs here is what an address's sets are shaped like -- a few numbers
 * spread over a long chain, or tens of thousands in one span -- and the sets carry both shapes
 * without the caller knowing which.
 */

/** Keys per bucket of the address map's key vector: 1024 keys of 32 bytes, 32 KiB a bucket. */
#define INDEX_KEYS_BUCKET_LOG2 10u
/** Address entries per bucket: 512 entries of 72 bytes. */
#define INDEX_SETS_BUCKET_LOG2 9u
/** Days per bucket: 1024 days, 4 KiB a bucket -- near three years in one. */
#define INDEX_DAYS_BUCKET_LOG2 10u
/** Foreign coin communities per bucket: 64. A chain meets few of them, then perhaps many. */
#define INDEX_COINS_BUCKET_LOG2 6u
/** Seconds in a day, the step the day table counts in. */
#define INDEX_SECONDS_PER_DAY 86400

// ********** small readings *******************

/** The day @p seconds falls in, counted from the epoch, going down for seconds before it. */
static int64_t day_of_second(int64_t seconds) {
  return seconds >= 0 ? seconds / INDEX_SECONDS_PER_DAY
                      : -(((-seconds) + INDEX_SECONDS_PER_DAY - 1) / INDEX_SECONDS_PER_DAY);
}

/** Whether every byte of a 32 byte public key is zero -- the sender a creation does not have. */
static bool is_zero_key(const uint8_t *key) {
  for (uint32_t i = 0; i < SIGN_PUBLIC_KEY_SIZE; ++i) {
    if (key[i]) { return false; }
  }
  return true;
}

/** The sets of @p id, which the map handed out and the vector holds. */
static grdb_address_sets *sets_at(const grdb_transactions *index, uint32_t id) {
  return (grdb_address_sets *)arnm_bvec_get(&index->address_sets, id);
}

static arnm_roaring_bitmap *coin_set_at(const grdb_transactions *index, uint32_t id) {
  return (arnm_roaring_bitmap *)arnm_bvec_get(&index->coin_sets, id);
}

/**
 * The entry a day holds: the largest offset confirmed that day, counted from one so that a
 * zero stays "no transaction this day" even for the very first transaction of a chain.
 */
static uint32_t *day_slot(const grdb_transactions *index, uint32_t day_index) {
  return (uint32_t *)arnm_bvec_get(&index->day_max_tx, day_index);
}

/** Whether @p set holds @p offset as its largest value -- this transaction is already in it. */
static bool set_ends_at(const arnm_roaring_bitmap *set, uint32_t offset) {
  uint32_t maximum = 0;
  return arnm_roaring_maximum(set, &maximum) && maximum == offset;
}

// ********** building *******************

arnm_result grdb_transactions_init(
    grdb_transactions *index, const grdb_transactions_options *options, arnm *source
) {
  if (!index) { return ARNM_ERROR_NULL_POINTER; }
  const grdb_transactions_options empty = {0};
  if (!options) { options = &empty; }

  memset(index, 0, sizeof(*index));
  index->source = source;
  index->base_tx_nr = options->base_tx_nr;

  arnm_graded_block_pool_options pool_options = {0};
  arnm_result result = arnm_graded_block_pool_init(&index->pool, &pool_options, source);
  if (ARNM_SUCCESS != result) { return result; }

  result =
      arnm_key_map_init(&index->addresses, SIGN_PUBLIC_KEY_SIZE, INDEX_KEYS_BUCKET_LOG2, source);
  if (ARNM_SUCCESS != result) { goto fail_pool; }

  result = arnm_bvec_init(
      &index->address_sets, INDEX_SETS_BUCKET_LOG2, 0, sizeof(grdb_address_sets), source
  );
  if (ARNM_SUCCESS != result) { goto fail_map; }

  result = arnm_bvec_init(&index->day_max_tx, INDEX_DAYS_BUCKET_LOG2, 0, sizeof(uint32_t), source);
  if (ARNM_SUCCESS != result) { goto fail_sets; }

  result = arnm_key_map_init(
      &index->coin_communities, ARNM_UUID_BINARY_SIZE, INDEX_COINS_BUCKET_LOG2, source
  );
  if (ARNM_SUCCESS != result) { goto fail_days_only; }

  result = arnm_bvec_init(
      &index->coin_sets, INDEX_COINS_BUCKET_LOG2, 0, sizeof(arnm_roaring_bitmap), source
  );
  if (ARNM_SUCCESS != result) { goto fail_coin_map; }

  if (options->expected_addresses) {
    result = arnm_key_map_reserve(&index->addresses, options->expected_addresses);
    if (ARNM_SUCCESS == result) {
      result = arnm_bvec_reserve(&index->address_sets, options->expected_addresses);
    }
    if (ARNM_SUCCESS != result) { goto fail_days; }
  }
  if (options->expected_days) {
    result = arnm_bvec_reserve(&index->day_max_tx, options->expected_days);
    if (ARNM_SUCCESS != result) { goto fail_days; }
  }

  index->ready = true;
  return ARNM_SUCCESS;

fail_days:
  arnm_bvec_free(&index->coin_sets);
fail_coin_map:
  arnm_key_map_free(&index->coin_communities);
fail_days_only:
  arnm_bvec_free(&index->day_max_tx);
fail_sets:
  arnm_bvec_free(&index->address_sets);
fail_map:
  arnm_key_map_free(&index->addresses);
fail_pool:
  arnm_graded_block_pool_release(&index->pool, source);
  memset(index, 0, sizeof(*index));
  return result;
}

/** Gives every set of every address back, and the type and coin sets with them. */
static void free_sets(grdb_transactions *index) {
  const uint32_t addresses = arnm_bvec_size(&index->address_sets);
  for (uint32_t id = 0; id < addresses; ++id) {
    grdb_address_sets *sets = sets_at(index, id);
    arnm_roaring_free(&sets->balance, &index->pool);
    arnm_roaring_free(&sets->signed_, &index->pool);
    arnm_roaring_free(&sets->other, &index->pool);
  }
  for (uint32_t type = 0; type < GRDT_TRANSACTION_COUNT; ++type) {
    arnm_roaring_free(&index->per_type[type], &index->pool);
  }
  const uint32_t coins = arnm_bvec_size(&index->coin_sets);
  for (uint32_t id = 0; id < coins; ++id) {
    arnm_roaring_free(coin_set_at(index, id), &index->pool);
  }
  arnm_roaring_free(&index->foreign, &index->pool);
}

void grdb_transactions_release(grdb_transactions *index) {
  if (!index || !index->ready) { return; }
  arnm *source = index->source;
  free_sets(index);
  arnm_bvec_free(&index->coin_sets);
  arnm_key_map_free(&index->coin_communities);
  arnm_bvec_free(&index->day_max_tx);
  arnm_bvec_free(&index->address_sets);
  arnm_key_map_free(&index->addresses);
  arnm_graded_block_pool_release(&index->pool, source);
  memset(index, 0, sizeof(*index));
}

void grdb_transactions_reset(grdb_transactions *index) {
  if (!index || !index->ready) { return; }
  // the sets go back to the pool, which keeps their blocks for the next fill
  free_sets(index);
  memset(index->per_type, 0, sizeof(index->per_type));
  memset(&index->foreign, 0, sizeof(index->foreign));
  memset(index->chain_community_uuid, 0, sizeof(index->chain_community_uuid));
  arnm_key_map_clear(&index->coin_communities);
  arnm_bvec_clear(&index->coin_sets);
  arnm_key_map_clear(&index->addresses);
  arnm_bvec_clear(&index->address_sets);
  arnm_bvec_clear(&index->day_max_tx);
  index->min_tx_nr = 0;
  index->max_tx_nr = 0;
  index->first_day = 0;
  index->transaction_count = 0;
}

/** The address's sets, opened on first sight of the key. */
static arnm_result sets_for_key(
    grdb_transactions *index, const uint8_t *key, grdb_address_sets **out
) {
  uint32_t id = 0;
  bool inserted = false;
  const arnm_result result = arnm_key_map_get_or_insert(&index->addresses, key, &id, &inserted);
  if (ARNM_SUCCESS != result) { return result; }
  if (inserted) {
    void *slot = NULL;
    const arnm_result grown = arnm_bvec_emplace(&index->address_sets, &slot);
    if (ARNM_SUCCESS != grown) {
      // the key stays in the map without sets; the next add for it finds the slot missing, so
      // the vector is filled up to the map's size before anything reads it
      return grown;
    }
    memset(slot, 0, sizeof(grdb_address_sets));
  }
  if (id >= arnm_bvec_size(&index->address_sets)) { return ARNM_ERROR_OUT_OF_MEMORY; }
  *out = sets_at(index, id);
  return ARNM_SUCCESS;
}

/**
 * The set of a foreign coin community, opened on first sight of its uuid.
 *
 * No limit on how many: which communities trade with which is not something a chain can know
 * in advance, and a community that has been running for years may pair with all of them.
 */
static arnm_result coin_set_for_uuid(
    grdb_transactions *index, const uint8_t *uuid, arnm_roaring_bitmap **out
) {
  uint32_t id = 0;
  bool inserted = false;
  const arnm_result result =
      arnm_key_map_get_or_insert(&index->coin_communities, uuid, &id, &inserted);
  if (ARNM_SUCCESS != result) { return result; }
  // grown up to the id rather than by one, so a key left without its set by an earlier refusal
  // finds it on the next try instead of reading past the vector
  while (arnm_bvec_size(&index->coin_sets) <= id) {
    void *slot = NULL;
    const arnm_result grown = arnm_bvec_emplace(&index->coin_sets, &slot);
    if (ARNM_SUCCESS != grown) { return grown; }
    memset(slot, 0, sizeof(arnm_roaring_bitmap));
  }
  *out = coin_set_at(index, id);
  return ARNM_SUCCESS;
}

/** Writes the day's largest offset, growing the table to reach the day. */
static arnm_result note_day(grdb_transactions *index, int64_t seconds, uint32_t offset) {
  const int64_t day = day_of_second(seconds);
  if (!index->transaction_count) { index->first_day = day; }
  if (day < index->first_day) { return ARNM_ERROR_INVALID_PARAM; }
  const int64_t distance = day - index->first_day;
  if (distance > (int64_t)UINT32_MAX) { return ARNM_ERROR_RESOURCE_SIZE_EXCEED; }
  const uint32_t day_index = (uint32_t)distance;
  while (arnm_bvec_size(&index->day_max_tx) <= day_index) {
    // days without a transaction stay zero, and a span steps over them
    const uint32_t empty = 0;
    const arnm_result grown = arnm_bvec_push_ptr(&index->day_max_tx, &empty);
    if (ARNM_SUCCESS != grown) { return grown; }
  }
  uint32_t *slot = day_slot(index, day_index);
  if (*slot < offset + 1u) { *slot = offset + 1u; }
  return ARNM_SUCCESS;
}

arnm_result grdb_transactions_add(grdb_transactions *index, const grdr_complete_transaction *tx) {
  if (!index || !tx) { return ARNM_ERROR_NULL_POINTER; }
  if (!index->ready) { return ARNM_ERROR_INVALID_STATE; }
  if (tx->tx_nr < index->base_tx_nr) { return ARNM_ERROR_INVALID_PARAM; }
  // a chain numbers a transaction as it completes it, one above the one before: no gaps, so
  // the span between two numbers holds exactly the transactions between them
  if (index->transaction_count && tx->tx_nr != index->max_tx_nr + 1u) {
    return ARNM_ERROR_INVALID_PARAM;
  }
  const uint64_t distance = tx->tx_nr - index->base_tx_nr;
  if (distance > (uint64_t)UINT32_MAX) { return ARNM_ERROR_RESOURCE_SIZE_EXCEED; }
  const uint32_t offset = (uint32_t)distance;
  if ((uint32_t)tx->transaction_type >= GRDT_TRANSACTION_COUNT) {
    return ARNM_ERROR_INVALID_ENUM_TYPE;
  }

  arnm_result result = note_day(index, tx->confirmed_at.seconds, offset);
  if (ARNM_SUCCESS != result) { return result; }

  // what the first transaction says about the chain has to stand before its own balances are
  // read: they are compared against the chain's community, and an unset one makes its own coin
  // look foreign. Only the count below says the transaction is in, so a refusal further down
  // leaves an index that still holds nothing.
  if (!index->transaction_count) {
    index->min_tx_nr = tx->tx_nr;
    memcpy(index->chain_community_uuid, tx->tx_community_uuid, ARNM_UUID_BINARY_SIZE);
  }

  // the signatures: whoever signed
  for (size_t i = 0; i < tx->signature_pairs_count; ++i) {
    const uint8_t *key = tx->signature_pairs[i].public_key;
    if (is_zero_key(key)) { continue; }
    grdb_address_sets *sets = NULL;
    result = sets_for_key(index, key, &sets);
    if (ARNM_SUCCESS != result) { return result; }
    result = arnm_roaring_add(&sets->signed_, offset, &index->pool);
    if (ARNM_SUCCESS != result) { return result; }
  }

  // the account balances: whose balance the transaction moved, and in which coin
  for (size_t i = 0; i < tx->account_balances_count; ++i) {
    const grdw_account_balance *balance = &tx->account_balances[i];
    if (!is_zero_key(balance->pubkey)) {
      grdb_address_sets *sets = NULL;
      result = sets_for_key(index, balance->pubkey, &sets);
      if (ARNM_SUCCESS != result) { return result; }
      result = arnm_roaring_add(&sets->balance, offset, &index->pool);
      if (ARNM_SUCCESS != result) { return result; }
    }
    if (0 != memcmp(balance->community_uuid, index->chain_community_uuid, ARNM_UUID_BINARY_SIZE)) {
      arnm_roaring_bitmap *coin = NULL;
      result = coin_set_for_uuid(index, balance->community_uuid, &coin);
      if (ARNM_SUCCESS != result) { return result; }
      result = arnm_roaring_add(coin, offset, &index->pool);
      if (ARNM_SUCCESS != result) { return result; }
      // and into the union, which is what "this chain's coin only" excludes
      result = arnm_roaring_add(&index->foreign, offset, &index->pool);
      if (ARNM_SUCCESS != result) { return result; }
    }
  }

  // the body: keys named without signing and without a balance entry
  const uint8_t *named[3] = {NULL, NULL, NULL};
  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_CREATION:
  case GRDT_TRANSACTION_TRANSFER:
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    named[0] = tx->transfer.sender_pubkey;
    named[1] = tx->transfer.recipient_pubkey;
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    named[0] = tx->register_address.user_public_key;
    named[1] = tx->register_address.account_public_key;
    break;
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    named[0] = tx->community_root.public_key;
    named[1] = tx->community_root.gmw_public_key;
    named[2] = tx->community_root.auf_public_key;
    break;
  default:
    break;
  }
  for (uint32_t i = 0; i < 3; ++i) {
    if (!named[i] || is_zero_key(named[i])) { continue; }
    grdb_address_sets *sets = NULL;
    result = sets_for_key(index, named[i], &sets);
    if (ARNM_SUCCESS != result) { return result; }
    // the third role is what is left: neither a signature nor a balance entry named this key
    if (set_ends_at(&sets->signed_, offset) || set_ends_at(&sets->balance, offset)) { continue; }
    result = arnm_roaring_add(&sets->other, offset, &index->pool);
    if (ARNM_SUCCESS != result) { return result; }
  }

  result = arnm_roaring_add(&index->per_type[tx->transaction_type], offset, &index->pool);
  if (ARNM_SUCCESS != result) { return result; }

  index->max_tx_nr = tx->tx_nr;
  ++index->transaction_count;
  return ARNM_SUCCESS;
}

// ********** asking *******************

bool grdb_transactions_range_of_days(
    const grdb_transactions *index,
    int64_t from_second,
    int64_t to_second,
    uint64_t *min,
    uint64_t *max
) {
  if (!index || !index->ready || !index->transaction_count || !min || !max) { return false; }
  const uint32_t days = arnm_bvec_size(&index->day_max_tx);

  // the last day of the span that holds anything gives the end of it
  int64_t last = to_second ? day_of_second(to_second) - index->first_day : (int64_t)days - 1;
  if (last < 0) { return false; }
  if (last > (int64_t)days - 1) { last = (int64_t)days - 1; }
  int64_t first = from_second ? day_of_second(from_second) - index->first_day : 0;
  if (first < 0) { first = 0; }
  if (first > last) { return false; }

  int64_t cursor = last;
  while (cursor >= first && !*day_slot(index, (uint32_t)cursor)) { --cursor; }
  if (cursor < first) { return false; }
  const uint32_t end_offset = *day_slot(index, (uint32_t)cursor) - 1u;

  // everything up to and including the day before the span is what the span starts after; with
  // no such day the span starts at the first transaction the index holds, not at the base
  uint32_t start_offset = (uint32_t)(index->min_tx_nr - index->base_tx_nr);
  for (int64_t before = first - 1; before >= 0; --before) {
    const uint32_t noted = *day_slot(index, (uint32_t)before);
    if (noted) {
      start_offset = noted; // one past that day's largest, since the slot counts from one
      break;
    }
  }
  if (start_offset > end_offset) { return false; }
  *min = index->base_tx_nr + start_offset;
  *max = index->base_tx_nr + end_offset;
  return true;
}

/** The sets a filter reads, gathered into a query over offsets; false when nothing can match. */
static bool query_of_filter(
    const grdb_transactions *index,
    const grdb_transactions_filter *filter,
    arnm_roaring_query *query,
    const arnm_roaring_bitmap *all[ARNM_ROARING_QUERY_MAX],
    const arnm_roaring_bitmap *any[ARNM_ROARING_QUERY_MAX],
    const arnm_roaring_bitmap *none[ARNM_ROARING_QUERY_MAX]
) {
  memset(query, 0, sizeof(*query));
  if (!index->transaction_count) { return false; }
  query->all = all;
  query->any = any;
  query->none = none;

  if (GRDB_ADDRESS_ROLE_NONE != filter->role) {
    uint32_t id = 0;
    if (!arnm_key_map_find(&index->addresses, filter->public_key, &id) ||
        id >= arnm_bvec_size(&index->address_sets)) {
      return false;
    }
    const grdb_address_sets *sets = sets_at(index, id);
    if (GRDB_ADDRESS_ROLE_BALANCE == filter->role) {
      all[query->all_count++] = &sets->balance;
    } else {
      // involved: in any of the three, which is a union the query reads without building it
      any[query->any_count++] = &sets->balance;
      any[query->any_count++] = &sets->signed_;
      any[query->any_count++] = &sets->other;
    }
  }

  if (GRDT_TRANSACTION_NONE != filter->transaction_type) {
    if ((uint32_t)filter->transaction_type >= GRDT_TRANSACTION_COUNT) { return false; }
    all[query->all_count++] = &index->per_type[filter->transaction_type];
  }

  const uint8_t *coin_community_uuid = grdb_transactions_filter_coin_community(filter);
  if (coin_community_uuid) {
    if (0 == memcmp(coin_community_uuid, index->chain_community_uuid, ARNM_UUID_BINARY_SIZE)) {
      // the chain's own coin: every transaction outside the union of the foreign ones -- one
      // set to exclude, however many foreign coins the chain has met
      none[query->none_count++] = &index->foreign;
    } else {
      uint32_t id = 0;
      if (!arnm_key_map_find(&index->coin_communities, coin_community_uuid, &id) ||
          id >= arnm_bvec_size(&index->coin_sets)) {
        return false;
      }
      all[query->all_count++] = coin_set_at(index, id);
    }
  }

  // the range: the numbers the filter names, narrowed by the days it names
  uint64_t min_tx = filter->min_tx_nr ? filter->min_tx_nr : index->min_tx_nr;
  uint64_t max_tx = filter->max_tx_nr ? filter->max_tx_nr : index->max_tx_nr;
  if (filter->from_seconds || filter->to_seconds) {
    uint64_t day_min = 0, day_max = 0;
    if (!grdb_transactions_range_of_days(
            index, filter->from_seconds, filter->to_seconds, &day_min, &day_max
        )) {
      return false;
    }
    if (day_min > min_tx) { min_tx = day_min; }
    if (day_max < max_tx) { max_tx = day_max; }
  }
  // a span can only hold what the index holds. A bound reaching past either end names numbers
  // that are no transactions, and a filter naming no set is answered from the span itself --
  // it would count and page those numbers as if they were.
  if (min_tx < index->min_tx_nr) { min_tx = index->min_tx_nr; }
  if (max_tx > index->max_tx_nr) { max_tx = index->max_tx_nr; }
  if (min_tx < index->base_tx_nr) { min_tx = index->base_tx_nr; }
  if (max_tx < min_tx) { return false; }
  if (max_tx - index->base_tx_nr > (uint64_t)UINT32_MAX) {
    max_tx = index->base_tx_nr + UINT32_MAX;
  }
  if (min_tx - index->base_tx_nr > (uint64_t)UINT32_MAX) { return false; }
  query->min = (uint32_t)(min_tx - index->base_tx_nr);
  query->max = (uint32_t)(max_tx - index->base_tx_nr);
  return true;
}

/**
 * A filter that names no set is the span itself: the numbers run without gaps, so every number
 * between the ends is a transaction. What a @c none list takes out is the only thing left to
 * ask a set about.
 */
static bool names_no_set(const arnm_roaring_query *query) {
  return !query->all_count && !query->any_count;
}

/** Whether a @c none set holds this offset; one binary search per set, and there are few. */
static bool span_excludes(const arnm_roaring_query *query, uint32_t offset) {
  for (uint32_t i = 0; i < query->none_count; ++i) {
    if (arnm_roaring_contains(query->none[i], offset)) { return true; }
  }
  return false;
}

/** Numbers in the span that no @c none set holds. */
static arnm_result span_cardinality(const arnm_roaring_query *query, uint64_t *out) {
  uint64_t inside = (uint64_t)query->max - (uint64_t)query->min + 1u;
  if (query->none_count) {
    // what the excluded sets hold inside the span, counted as one union
    arnm_roaring_query excluded;
    memset(&excluded, 0, sizeof(excluded));
    excluded.any = query->none;
    excluded.any_count = query->none_count;
    excluded.min = query->min;
    excluded.max = query->max;
    uint64_t taken = 0;
    const arnm_result result = arnm_roaring_query_cardinality(&excluded, &taken);
    if (ARNM_SUCCESS != result) { return result; }
    inside = taken < inside ? inside - taken : 0u;
  }
  *out = inside;
  return ARNM_SUCCESS;
}

/**
 * One page of the span, the excluded numbers stepped over.
 *
 * Counts through the span from the end the page starts at. What that costs grows with what the
 * @c none sets take out -- for the sets this index keeps, transactions carrying a foreign coin,
 * that is a small part of a chain.
 */
static uint32_t span_page(
    const arnm_roaring_query *query, uint32_t skip, uint32_t size, bool descending, uint32_t *out
) {
  const uint64_t width = (uint64_t)query->max - (uint64_t)query->min + 1u;
  uint32_t written = 0;
  for (uint64_t step = 0; step < width && written < size; ++step) {
    const uint32_t offset = descending ? (uint32_t)((uint64_t)query->max - step)
                                       : (uint32_t)((uint64_t)query->min + step);
    if (span_excludes(query, offset)) { continue; }
    if (skip) {
      --skip;
      continue;
    }
    out[written++] = offset;
  }
  return written;
}

arnm_result grdb_transactions_count(
    const grdb_transactions *index, const grdb_transactions_filter *filter, uint64_t *out
) {
  if (!index || !filter || !out) { return ARNM_ERROR_NULL_POINTER; }
  if (!index->ready) { return ARNM_ERROR_INVALID_STATE; }
  arnm_roaring_query query;
  const arnm_roaring_bitmap *all[ARNM_ROARING_QUERY_MAX];
  const arnm_roaring_bitmap *any[ARNM_ROARING_QUERY_MAX];
  const arnm_roaring_bitmap *none[ARNM_ROARING_QUERY_MAX];
  if (!query_of_filter(index, filter, &query, all, any, none)) {
    *out = 0;
    return ARNM_SUCCESS;
  }
  if (names_no_set(&query)) { return span_cardinality(&query, out); }
  return arnm_roaring_query_cardinality(&query, out);
}

arnm_result grdb_transactions_listing(
    const grdb_transactions *index,
    const grdb_transactions_filter *filter,
    uint32_t skip,
    uint32_t size,
    bool descending,
    uint64_t *out,
    uint32_t *written,
    uint64_t *count
) {
  if (!index || !filter || !written || !count || (size && !out)) { return ARNM_ERROR_NULL_POINTER; }
  if (!index->ready) { return ARNM_ERROR_INVALID_STATE; }
  if (size > GRDB_PAGE_MAX) { return ARNM_ERROR_INVALID_PARAM; }

  arnm_roaring_query query;
  const arnm_roaring_bitmap *all[ARNM_ROARING_QUERY_MAX];
  const arnm_roaring_bitmap *any[ARNM_ROARING_QUERY_MAX];
  const arnm_roaring_bitmap *none[ARNM_ROARING_QUERY_MAX];
  if (!query_of_filter(index, filter, &query, all, any, none)) {
    *written = 0;
    *count = 0;
    return ARNM_SUCCESS;
  }

  uint32_t offsets[GRDB_PAGE_MAX];
  uint32_t taken = 0;
  uint64_t matches = 0;
  arnm_result result = ARNM_SUCCESS;
  if (names_no_set(&query)) {
    result = span_cardinality(&query, &matches);
    if (ARNM_SUCCESS == result && size) {
      taken = span_page(&query, skip, size, descending, offsets);
    }
  } else {
    result = arnm_roaring_query_listing(&query, skip, size, descending, offsets, &taken, &matches);
  }
  if (ARNM_SUCCESS != result) { return result; }
  for (uint32_t i = 0; i < taken; ++i) { out[i] = index->base_tx_nr + offsets[i]; }
  *written = taken;
  *count = matches;
  return ARNM_SUCCESS;
}

bool grdb_transactions_newest(
    const grdb_transactions *index, const grdb_transactions_filter *filter, uint64_t *out
) {
  if (!index || !filter || !out || !index->ready) { return false; }
  arnm_roaring_query query;
  const arnm_roaring_bitmap *all[ARNM_ROARING_QUERY_MAX];
  const arnm_roaring_bitmap *any[ARNM_ROARING_QUERY_MAX];
  const arnm_roaring_bitmap *none[ARNM_ROARING_QUERY_MAX];
  if (!query_of_filter(index, filter, &query, all, any, none)) { return false; }
  uint32_t offset = 0;
  uint32_t written = 0;
  if (names_no_set(&query)) {
    written = span_page(&query, 0, 1, true, &offset);
  } else if (ARNM_SUCCESS != arnm_roaring_query_page(&query, 0, 1, true, &offset, &written)) {
    return false;
  }
  if (!written) { return false; }
  *out = index->base_tx_nr + offset;
  return true;
}

// ********** reading what is in it *******************

uint32_t grdb_transactions_address_count(const grdb_transactions *index) {
  return index && index->ready ? arnm_key_map_size(&index->addresses) : 0u;
}

uint32_t grdb_transactions_coin_community_count(const grdb_transactions *index) {
  return index && index->ready ? arnm_key_map_size(&index->coin_communities) : 0u;
}

const grdb_address_sets *grdb_transactions_address(
    const grdb_transactions *index, const uint8_t *public_key
) {
  uint32_t id = 0;
  if (!index || !index->ready || !public_key) { return NULL; }
  if (!arnm_key_map_find(&index->addresses, public_key, &id)) { return NULL; }
  if (id >= arnm_bvec_size(&index->address_sets)) { return NULL; }
  return sets_at(index, id);
}
