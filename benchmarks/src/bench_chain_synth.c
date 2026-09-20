#include "bench_chain_synth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * The generator behind bench_chain_synth.h: a chain built from the shape of a real one. Every
 * number it needs comes from one splitmix64, so the same seed walks the same chain -- the
 * property a failing benchmark run is worth nothing without.
 */

/** Addresses a generated chain may hold; a chain of N transactions grows about N/3 of them. */
#define SYNTH_MAX_ADDRESSES 200000u
/** Deferred transfers waiting for a redeem or a timeout. */
#define SYNTH_PENDING_MAX 4096u
/** Signature and balance slots one transaction can fill. */
#define SYNTH_SIGNATURES_MAX 3u
#define SYNTH_BALANCES_MAX 3u
/** Mean seconds between two transactions, as measured on the real chain (4156). */
#define SYNTH_MEAN_GAP_SECONDS 8300u
/** The second the first transaction is confirmed at: 2019-07-01, where the real chain starts. */
#define SYNTH_FIRST_SECOND 1561939200

/** Transaction shares in percent, in the order the generator draws them. */
#define SYNTH_SHARE_CREATION 583u
#define SYNTH_SHARE_REGISTER 123u
#define SYNTH_SHARE_DEFERRED 102u
#define SYNTH_SHARE_TRANSFER 91u
#define SYNTH_SHARE_TIMEOUT 56u
#define SYNTH_SHARE_REDEEM 45u

typedef struct pending_deferred {
  uint32_t sender;    /**< address id */
  uint32_t recipient; /**< address id */
  uint64_t tx_nr;
} pending_deferred;

/** Everything the walk carries between transactions. */
typedef struct synth_state {
  uint64_t rng;
  uint8_t community_uuid[ARNM_UUID_BINARY_SIZE];
  uint8_t foreign_uuid[ARNM_UUID_BINARY_SIZE];
  uint8_t (*keys)[SIGN_PUBLIC_KEY_SIZE];
  uint32_t key_count;
  uint32_t gmw;     /**< the community's public budget account */
  uint32_t auf;     /**< the community's compensation account */
  uint32_t signer;  /**< signs every creation, as one account does on the real chain */
  uint32_t *humans; /**< registered user keys, the recipients of creations */
  uint32_t human_count;
  pending_deferred pending[SYNTH_PENDING_MAX];
  uint32_t pending_count;
  int64_t seconds;
  uint64_t tx_nr;
  grdw_signature_pair signatures[SYNTH_SIGNATURES_MAX];
  grdw_account_balance balances[SYNTH_BALANCES_MAX];
} synth_state;

/** splitmix64: one multiply-xor-shift chain, the same numbers everywhere. */
static uint64_t synth_next(synth_state *state) {
  uint64_t z = (state->rng += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

static uint32_t synth_below(synth_state *state, uint32_t bound) {
  return bound ? (uint32_t)(synth_next(state) % bound) : 0u;
}

/** A new address, its 32 bytes filled from the generator so the same seed gives the same keys. */
static uint32_t synth_new_key(synth_state *state) {
  if (state->key_count == SYNTH_MAX_ADDRESSES) { return synth_below(state, state->key_count); }
  uint8_t *key = state->keys[state->key_count];
  for (uint32_t i = 0; i < SIGN_PUBLIC_KEY_SIZE; i += 8u) {
    const uint64_t bits = synth_next(state);
    memcpy(key + i, &bits, 8u);
  }
  key[0] |= 1u; // never the all zero key, which is no address
  return state->key_count++;
}

/** Signs with the addresses named, which is what decides how many a transaction touches. */
static void synth_sign(
    synth_state *state, grdr_complete_transaction *tx, const uint32_t *addresses, uint32_t count
) {
  for (uint32_t i = 0; i < count; ++i) {
    memcpy(state->signatures[i].public_key, state->keys[addresses[i]], SIGN_PUBLIC_KEY_SIZE);
  }
  tx->signature_pairs = count ? state->signatures : NULL;
  tx->signature_pairs_count = count;
}

/**
 * A human, drawn so that a few are in almost everything and most are in one transaction: a
 * bound drawn from a bound, which lands low far more often than high. The real chain's ten
 * busiest addresses hold well over half of all involvement, and a flat draw never does that.
 */
static uint32_t synth_human(synth_state *state) {
  const uint32_t bound = synth_below(state, state->human_count) + 1u;
  return state->humans[synth_below(state, bound)];
}

static void synth_balance(synth_state *state, uint32_t slot, uint32_t address, bool foreign) {
  memset(&state->balances[slot], 0, sizeof(state->balances[slot]));
  memcpy(state->balances[slot].pubkey, state->keys[address], SIGN_PUBLIC_KEY_SIZE);
  memcpy(
      state->balances[slot].community_uuid, foreign ? state->foreign_uuid : state->community_uuid,
      ARNM_UUID_BINARY_SIZE
  );
}

/** Writes the next transaction of the chain into @p tx. */
static void synth_transaction(synth_state *state, grdr_complete_transaction *tx) {
  grdr_complete_transaction_init(tx);
  tx->tx_nr = ++state->tx_nr;
  state->seconds += 1 + (int64_t)synth_below(state, SYNTH_MEAN_GAP_SECONDS);
  tx->confirmed_at.seconds = state->seconds;
  tx->created_at.seconds = state->seconds;
  memcpy(tx->tx_community_uuid, state->community_uuid, ARNM_UUID_BINARY_SIZE);
  tx->account_balances = state->balances;

  if (1u == tx->tx_nr) {
    // the root that opens a chain: the community, its two accounts, and nobody signing
    tx->transaction_type = GRDT_TRANSACTION_COMMUNITY_ROOT;
    const uint32_t community = synth_new_key(state);
    state->gmw = synth_new_key(state);
    state->auf = synth_new_key(state);
    state->signer = community;
    (void)0;
    memcpy(tx->community_root.public_key, state->keys[community], SIGN_PUBLIC_KEY_SIZE);
    memcpy(tx->community_root.gmw_public_key, state->keys[state->gmw], SIGN_PUBLIC_KEY_SIZE);
    memcpy(tx->community_root.auf_public_key, state->keys[state->auf], SIGN_PUBLIC_KEY_SIZE);
    synth_sign(state, tx, NULL, 0);
    tx->account_balances_count = 0;
    return;
  }

  const bool foreign = synth_below(state, 100u) < BENCH_CHAIN_FOREIGN_COIN_PERCENT;
  uint32_t draw = synth_below(state, 1000u);
  // a redeem or a timeout needs a deferred transfer waiting, and a creation needs a human
  if (!state->pending_count) { draw = draw % (1000u - SYNTH_SHARE_TIMEOUT - SYNTH_SHARE_REDEEM); }
  if (!state->human_count && draw < SYNTH_SHARE_CREATION) { draw = SYNTH_SHARE_CREATION; }

  if (draw < SYNTH_SHARE_CREATION) {
    // a creation: to a human, counted in the community's two accounts, signed by the community
    tx->transaction_type = GRDT_TRANSACTION_CREATION;
    const uint32_t human = synth_human(state);
    memcpy(tx->transfer.recipient_pubkey, state->keys[human], SIGN_PUBLIC_KEY_SIZE);
    tx->target_date = state->seconds;
    synth_balance(state, 0, human, false);
    synth_balance(state, 1, state->gmw, false);
    synth_balance(state, 2, state->auf, foreign);
    tx->account_balances_count = 3;
    const uint32_t signers[1] = {state->signer};
    synth_sign(state, tx, signers, 1);
    return;
  }
  draw -= SYNTH_SHARE_CREATION;

  if (draw < SYNTH_SHARE_REGISTER) {
    // a registration: two new addresses, one balance, three signatures
    tx->transaction_type = GRDT_TRANSACTION_REGISTER_ADDRESS;
    const uint32_t user = synth_new_key(state);
    const uint32_t account = synth_new_key(state);
    if (state->human_count < SYNTH_MAX_ADDRESSES) { state->humans[state->human_count++] = user; }
    tx->address_type = GRDT_ADDRESS_COMMUNITY_HUMAN;
    tx->derivation_index = 1;
    memcpy(tx->register_address.user_public_key, state->keys[user], SIGN_PUBLIC_KEY_SIZE);
    memcpy(tx->register_address.account_public_key, state->keys[account], SIGN_PUBLIC_KEY_SIZE);
    synth_balance(state, 0, account, false);
    tx->account_balances_count = 1;
    // the community and both new keys sign their own registration: three signatures, three
    // addresses, which is the shape the real chain has
    const uint32_t signers[3] = {state->signer, user, account};
    synth_sign(state, tx, signers, 3);
    return;
  }
  draw -= SYNTH_SHARE_REGISTER;

  if (draw < SYNTH_SHARE_DEFERRED) {
    // a deferred transfer: to an address that exists for this transfer alone
    tx->transaction_type = GRDT_TRANSACTION_DEFERRED_TRANSFER;
    const uint32_t sender = synth_human(state);
    const uint32_t recipient = synth_new_key(state);
    memcpy(tx->transfer.sender_pubkey, state->keys[sender], SIGN_PUBLIC_KEY_SIZE);
    memcpy(tx->transfer.recipient_pubkey, state->keys[recipient], SIGN_PUBLIC_KEY_SIZE);
    tx->timeout_duration = 30 * 86400;
    synth_balance(state, 0, sender, false);
    synth_balance(state, 1, recipient, foreign);
    tx->account_balances_count = 2;
    const uint32_t signers[1] = {sender};
    synth_sign(state, tx, signers, 1);
    if (state->pending_count < SYNTH_PENDING_MAX) {
      state->pending[state->pending_count].sender = sender;
      state->pending[state->pending_count].recipient = recipient;
      state->pending[state->pending_count].tx_nr = tx->tx_nr;
      ++state->pending_count;
    }
    return;
  }
  draw -= SYNTH_SHARE_DEFERRED;

  if (draw < SYNTH_SHARE_TRANSFER) {
    // a transfer between two addresses that are already there
    tx->transaction_type = GRDT_TRANSACTION_TRANSFER;
    const uint32_t sender = synth_human(state);
    const uint32_t recipient = synth_human(state);
    memcpy(tx->transfer.sender_pubkey, state->keys[sender], SIGN_PUBLIC_KEY_SIZE);
    memcpy(tx->transfer.recipient_pubkey, state->keys[recipient], SIGN_PUBLIC_KEY_SIZE);
    synth_balance(state, 0, sender, false);
    synth_balance(state, 1, recipient, foreign);
    tx->account_balances_count = 2;
    const uint32_t signers[1] = {sender};
    synth_sign(state, tx, signers, 1);
    return;
  }
  draw -= SYNTH_SHARE_TRANSFER;

  // what is left closes a deferred transfer: a timeout nobody signs, or a redeem the sender does
  const uint32_t which = synth_below(state, state->pending_count);
  const pending_deferred closed = state->pending[which];
  state->pending[which] = state->pending[--state->pending_count];
  memcpy(tx->transfer.sender_pubkey, state->keys[closed.recipient], SIGN_PUBLIC_KEY_SIZE);
  memcpy(tx->transfer.recipient_pubkey, state->keys[closed.sender], SIGN_PUBLIC_KEY_SIZE);
  tx->previous_tx = closed.tx_nr;
  synth_balance(state, 0, closed.recipient, false);
  synth_balance(state, 1, closed.sender, foreign);
  if (draw < SYNTH_SHARE_TIMEOUT) {
    // a timeout is the chain's own doing: nobody signs it
    tx->transaction_type = GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER;
    tx->account_balances_count = 2;
    synth_sign(state, tx, NULL, 0);
    return;
  }
  tx->transaction_type = GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER;
  synth_balance(state, 2, state->gmw, false);
  tx->account_balances_count = 3;
  const uint32_t signers[1] = {closed.recipient};
  synth_sign(state, tx, signers, 1);
}

// ********** where a run's transactions come from *******************

uint64_t bench_chain_seed(void) {
  const char *text = getenv("GRD_CHAIN_SEED");
  if (!text || !*text) { return BENCH_CHAIN_DEFAULT_SEED; }
  if (0 == strcmp(text, "NOW") || 0 == strcmp(text, "now")) {
    return (uint64_t)time(NULL) ^ 0x5851f42d4c957f2dull;
  }
  return strtoull(text, NULL, 10);
}

uint32_t bench_chain_count(void) {
  const char *text = getenv("GRD_CHAIN_COUNT");
  if (!text || !*text) { return BENCH_CHAIN_DEFAULT_COUNT; }
  const unsigned long long count = strtoull(text, NULL, 10);
  return count && count <= UINT32_MAX ? (uint32_t)count : BENCH_CHAIN_DEFAULT_COUNT;
}

bench_chain_source bench_chain_source_of(int argc, char **argv) {
  bench_chain_source source;
  source.path = argc > 1 && argv ? argv[1] : getenv("GRD_BENCH_CHAIN_DATA");
  if (source.path && !*source.path) { source.path = NULL; }
  source.count = bench_chain_count();
  source.seed = bench_chain_seed();
  return source;
}

void bench_chain_source_print(const bench_chain_source *source) {
  if (!source) { return; }
  if (source->path) {
    printf("chain %s\n", source->path);
    return;
  }
  printf(
      "chain generated: %u transactions, seed %llu (GRD_CHAIN_SEED=%llu repeats it)\n",
      source->count, (unsigned long long)source->seed, (unsigned long long)source->seed
  );
}

/** Generates @p count transactions and hands each one over. */
static arnm_result synth_feed(
    uint32_t count,
    uint64_t seed,
    const uint8_t community_uuid[16],
    bench_chain_feed_fn fn,
    void *context,
    uint32_t *transactions
) {
  synth_state *state = (synth_state *)calloc(1, sizeof(synth_state));
  if (!state) { return ARNM_ERROR_OUT_OF_MEMORY; }
  state->keys =
      (uint8_t (*)[SIGN_PUBLIC_KEY_SIZE])malloc((size_t)SYNTH_MAX_ADDRESSES * SIGN_PUBLIC_KEY_SIZE);
  state->humans = (uint32_t *)malloc((size_t)SYNTH_MAX_ADDRESSES * sizeof(uint32_t));
  if (!state->keys || !state->humans) {
    free(state->humans);
    free(state->keys);
    free(state);
    return ARNM_ERROR_OUT_OF_MEMORY;
  }
  state->rng = seed;
  state->seconds = SYNTH_FIRST_SECOND;
  memcpy(state->community_uuid, community_uuid, ARNM_UUID_BINARY_SIZE);
  memcpy(state->foreign_uuid, community_uuid, ARNM_UUID_BINARY_SIZE);
  state->foreign_uuid[0] = (uint8_t)(state->foreign_uuid[0] ^ 0xffu);

  grdr_complete_transaction tx;
  arnm_result result = ARNM_SUCCESS;
  uint32_t written = 0;
  for (; written < count; ++written) {
    synth_transaction(state, &tx);
    result = fn(&tx, context);
    if (ARNM_SUCCESS != result) { break; }
  }
  if (transactions) { *transactions = written; }
  free(state->humans);
  free(state->keys);
  free(state);
  return result;
}

arnm_result bench_chain_source_feed(
    const bench_chain_source *source,
    const uint8_t community_uuid[16],
    bench_chain_feed_fn fn,
    void *context,
    uint32_t *transactions
) {
  if (!source || !fn) { return ARNM_ERROR_NULL_POINTER; }
  if (source->path) {
    return bench_chain_feed(source->path, community_uuid, fn, context, transactions);
  }
  return synth_feed(source->count, source->seed, community_uuid, fn, context, transactions);
}
