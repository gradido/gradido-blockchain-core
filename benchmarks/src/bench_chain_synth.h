#ifndef GRADIDO_BLOCKCHAIN_CORE_BENCH_CHAIN_SYNTH_H
#define GRADIDO_BLOCKCHAIN_CORE_BENCH_CHAIN_SYNTH_H

#include "bench_chain_feed.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A chain that never existed, shaped like the one that did.
 *
 * Tests and benchmarks need a chain longer than the file in the tree and available where that
 * file is not. Random transactions would answer a different question than a chain does, so the
 * generator carries the shape measured on `blk00000001.dat` (48763 transactions, 16927
 * addresses, 2342 days):
 *
 * | transaction        | share  | signatures | balances | addresses |
 * |--------------------|--------|------------|----------|-----------|
 * | creation           | 58.3 % | 1          | 3        | 4         |
 * | register address   | 12.3 % | 3          | 1        | 3         |
 * | deferred transfer  | 10.2 % | 1          | 2        | 2         |
 * | transfer           |  9.1 % | 1          | 2        | 2         |
 * | timeout deferred   |  5.6 % | 0          | 2        | 2         |
 * | redeem deferred    |  4.6 % | 1          | 3        | 3         |
 *
 * A community root opens the chain. Addresses arrive the way they do there: two per
 * registration, one fresh recipient per deferred transfer -- which is why a real chain of that
 * length holds about a third as many addresses as transactions. Creations carry the community's
 * two accounts, so a handful of addresses appear in most transactions while the median address
 * appears once, the shape every query over this index meets.
 *
 * Foreign coins are the one thing not taken from the file: every balance there is in the same
 * community, so the generator puts a small share in another coin to give that path something to
 * find.
 *
 * ### Same seed, same chain
 *
 * The generator is a splitmix64 and nothing else, so a seed gives the same chain on every
 * platform and in every build. @ref bench_chain_seed() reads `GRD_CHAIN_SEED`: a number, or
 * `NOW` for the clock -- which is how a run looks for what a fixed seed never shows. Whatever it
 * resolves to is printed by the callers, so a chain that found something can be built again.
 *
 * @note A transaction handed out points into the generator's own buffers and lasts until the
 *       next one. An index reads it and keeps nothing, which is what this is for.
 */

/** The seed a run uses when the environment names none. */
#define BENCH_CHAIN_DEFAULT_SEED 0x9e3779b97f4a7c15ull
/** Transactions a generated chain holds when the environment names no other count. */
#define BENCH_CHAIN_DEFAULT_COUNT 100000u
/** Share of transactions whose balance is in another community's coin, in percent. */
#define BENCH_CHAIN_FOREIGN_COIN_PERCENT 2u

/** Where a run's transactions come from. */
typedef struct bench_chain_source {
  const char *path; /**< A chain file, or NULL to generate one. */
  uint32_t count;   /**< Transactions to generate; meaningless with a path. */
  uint64_t seed;    /**< The seed used to generate them. */
} bench_chain_source;

/**
 * The seed for this run: `GRD_CHAIN_SEED` as a decimal number, or `NOW` for the wall clock,
 * or @ref BENCH_CHAIN_DEFAULT_SEED when it is not set.
 */
uint64_t bench_chain_seed(void);

/** Transactions to generate: `GRD_CHAIN_COUNT`, or @ref BENCH_CHAIN_DEFAULT_COUNT. */
uint32_t bench_chain_count(void);

/**
 * What a test or benchmark should read: the first argument or `GRD_BENCH_CHAIN_DATA` names a
 * file, and without one a chain is generated from @ref bench_chain_seed().
 *
 * @param[in] argc As main() received it; 0 to ignore the command line.
 * @param[in] argv As main() received it; may be NULL.
 */
bench_chain_source bench_chain_source_of(int argc, char **argv);

/** One line naming where the transactions come from, so a run can be repeated. */
void bench_chain_source_print(const bench_chain_source *source);

/**
 * Hands every transaction of @p source to @p fn -- read from its file, or generated.
 *
 * @param[in]     source         Where the chain comes from; not NULL.
 * @param[in]     community_uuid 16 bytes the transactions carry as their community.
 * @param[in]     fn             Called per transaction; not NULL.
 * @param[in,out] context        Passed through.
 * @param[out]    transactions   Receives how many were handed over; may be NULL.
 * @return As @ref bench_chain_feed(), and whatever @p fn refused with.
 */
arnm_result bench_chain_source_feed(
    const bench_chain_source *source,
    const uint8_t community_uuid[16],
    bench_chain_feed_fn fn,
    void *context,
    uint32_t *transactions
);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_BENCH_CHAIN_SYNTH_H
