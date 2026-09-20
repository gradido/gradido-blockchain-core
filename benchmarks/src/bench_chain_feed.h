#ifndef GRADIDO_BLOCKCHAIN_CORE_BENCH_CHAIN_FEED_H
#define GRADIDO_BLOCKCHAIN_CORE_BENCH_CHAIN_FEED_H

#include "gradido_blockchain_core/data/runtime/complete_transaction.h"

#include <stdint.h>
#include <stdio.h>

/**
 * A chain file read transaction by transaction, the way a node fills its indices at start: the
 * file is `[uint16 size][protobuf]` repeated, and each transaction is decoded into the same
 * struct, handed over, and released again. Nothing is kept, so a chain of any length costs what
 * one transaction costs.
 *
 * Header only, because it is the benchmarks that use it and they each link what they need.
 */

/** Scratch for one decode; a transaction of the chains measured stays far below it. */
#define BENCH_CHAIN_FEED_DECODE_BYTES (256u * 1024u)
/** Largest transaction the length prefix can name. */
#define BENCH_CHAIN_FEED_MAX_TX_BYTES 65535u

/**
 * Called for every transaction read. Returning anything but ARNM_SUCCESS stops the walk and
 * that value comes back from bench_chain_feed().
 */
typedef arnm_result (*bench_chain_feed_fn)(const grdr_complete_transaction *tx, void *context);

/**
 * Reads @p path and hands every transaction to @p fn.
 *
 * @param[in]  path           Chain file; not NULL.
 * @param[in]  community_uuid 16 bytes the decoded transactions carry as their community.
 * @param[in]  fn             Called per transaction; not NULL.
 * @param[in,out] context     Passed through to @p fn.
 * @param[out] transactions   Receives how many were handed over; may be NULL.
 * @retval ARNM_SUCCESS             The file was read to its end.
 * @retval ARNM_ERROR_INVALID_PARAM The file could not be opened, or it held no transaction.
 * @return Whatever @p fn refused with, which also stops the walk.
 */
static inline arnm_result bench_chain_feed(
    const char *path,
    const uint8_t community_uuid[16],
    bench_chain_feed_fn fn,
    void *context,
    uint32_t *transactions
) {
  if (!path || !fn) { return ARNM_ERROR_NULL_POINTER; }
  FILE *file = fopen(path, "rb");
  if (!file) { return ARNM_ERROR_INVALID_PARAM; }

  // words rather than bytes: the decoder wants 8 byte alignment, and an array of uint64_t has
  // it in C and in C++ alike, where the alignment keywords are spelled differently
  static uint64_t decode_words[BENCH_CHAIN_FEED_DECODE_BYTES / 8u];
  static uint8_t tx_buffer[BENCH_CHAIN_FEED_MAX_TX_BYTES];
  uint8_t *const decode_buffer = (uint8_t *)decode_words;
  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);

  uint32_t count = 0;
  arnm_result result = ARNM_SUCCESS;
  uint8_t size_bytes[2];
  while (2u == fread(size_bytes, 1, 2, file)) {
    const uint16_t size = (uint16_t)(size_bytes[0] | (size_bytes[1] << 8));
    if (!size || size != fread(tx_buffer, 1, size, file)) { break; }
    if (ARNM_SUCCESS !=
        grdr_complete_transaction_init_from_protobuf(
            &tx, tx_buffer, size, community_uuid, decode_buffer, (uint32_t)sizeof(decode_words)
        )) {
      continue; // a transaction this build cannot read is skipped, not counted
    }
    result = fn(&tx, context);
    grdr_complete_transaction_release(&tx);
    if (ARNM_SUCCESS != result) { break; }
    ++count;
  }
  fclose(file);
  if (transactions) { *transactions = count; }
  if (ARNM_SUCCESS == result && !count) { return ARNM_ERROR_INVALID_PARAM; }
  return result;
}

#endif // GRADIDO_BLOCKCHAIN_CORE_BENCH_CHAIN_FEED_H
