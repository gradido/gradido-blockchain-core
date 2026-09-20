#ifndef GRADIDO_BLOCKCHAIN_CORE_BENCH_CHAIN_DATA_H
#define GRADIDO_BLOCKCHAIN_CORE_BENCH_CHAIN_DATA_H

/*
 * Transaction index input for the tx_index benchmarks and prototype tests.
 *
 * A record is what the index needs from one confirmed transaction and nothing more: number,
 * confirmation time, type, the addresses it touches with the role each plays, and the foreign
 * coin communities among its balances. It is extracted once from grdr_complete_transaction, so
 * the benchmarks time the index and not the decoder.
 *
 * Two sources:
 *   - a real chain file, the format gradido_blockchain's LoadFromBinary test reads: repeated
 *     [uint16 size][ConfirmedTransaction protobuf], e.g. blk00000001.dat;
 *   - a synthetic chain from a seed, for sizes no real file has.
 */

#include "arnm/result.h"
#include "gradido_blockchain_core/types/transaction.h"
#include "proto/proto_common.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BENCH_TX_ADDRESS_MAX 12u
#define BENCH_TX_FOREIGN_COIN_MAX 3u
#define BENCH_COMMUNITY_MAX 64u
#define BENCH_UUID_SIZE 16u

/** What an address did in a transaction. Several may be set. */
typedef enum bench_address_role {
  BENCH_ADDRESS_SIGNER = 1,  /**< in the signature map */
  BENCH_ADDRESS_BALANCE = 2, /**< has an account balance entry, i.e. its balance changed */
  BENCH_ADDRESS_OTHER = 4    /**< named in the body without signing or a balance entry */
} bench_address_role;

typedef struct bench_tx_address {
  uint8_t key[PROTO_KEY_SIZE];
  uint8_t roles; /**< bench_address_role bits */
} bench_tx_address;

typedef struct bench_tx_record {
  uint64_t tx_nr;
  int64_t confirmed_seconds;
  uint8_t type; /**< grdt_transaction */
  uint8_t address_count;
  uint8_t foreign_coin_count;
  /** local community ids (1 based index into bench_chain_data.communities) of balances in a
   *  coin other than the chain's own */
  uint8_t foreign_coin[BENCH_TX_FOREIGN_COIN_MAX];
  bench_tx_address addresses[BENCH_TX_ADDRESS_MAX];
} bench_tx_record;

typedef struct bench_chain_data {
  bench_tx_record *records;
  uint32_t count;
  uint32_t capacity;
  /** [0] is the chain's own community, the rest foreign coin communities in order of sight */
  uint8_t communities[BENCH_COMMUNITY_MAX][BENCH_UUID_SIZE];
  uint32_t community_count;
  uint32_t decode_failures;
  uint32_t address_overflows; /**< transactions with more addresses than BENCH_TX_ADDRESS_MAX */
  uint64_t input_bytes;
} bench_chain_data;

/** Community of blk00000001.dat in gradido_blockchain/tests/data. */
extern const uint8_t bench_default_community_uuid[BENCH_UUID_SIZE];

/**
 * Chain file to use: argv[1] if given, else $GRD_BENCH_CHAIN_DATA, else NULL.
 */
const char *bench_chain_data_path(int argc, char **argv);

/**
 * @retval ARNM_ERROR_INVALID_PARAM the file could not be opened or read.
 */
arnm_result bench_chain_data_load(
    bench_chain_data *data, const char *path, const uint8_t community_uuid[BENCH_UUID_SIZE]
);

/**
 * Deterministic synthetic chain: @p tx_count transactions over @p key_count addresses whose
 * activity is skewed (a few addresses take most transactions, as on a real chain), 60 s apart
 * on average, types weighted towards transfers, 1 in 200 with a foreign coin balance.
 */
arnm_result bench_chain_data_synthetic(
    bench_chain_data *data, uint32_t tx_count, uint32_t key_count, uint64_t seed
);

void bench_chain_data_free(bench_chain_data *data);

/** splitmix64 step; the generator every synthetic set here draws from. */
static inline uint64_t bench_random_next(uint64_t *state) {
  *state += 0x9e3779b97f4a7c15ULL;
  return proto_mix64(*state);
}

static inline void bench_random_key(uint64_t *state, uint8_t key[PROTO_KEY_SIZE]) {
  for (unsigned word = 0; word < 4u; ++word) {
    uint64_t value = bench_random_next(state);
    memcpy(key + word * 8u, &value, sizeof(value));
  }
}

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_BENCH_CHAIN_DATA_H
