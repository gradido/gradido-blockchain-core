#ifndef GRADIDO_BLOCKCHAIN_CORE_CRYPTO_VALIDATION_H
#define GRADIDO_BLOCKCHAIN_CORE_CRYPTO_VALIDATION_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup grdc_validation grdc_validation
 * @ingroup crypto
 * @brief Checks on key material that need no cryptography: whether a key, a signature, a hash
 *        or a uuid was ever filled in.
 *
 * A public key of 32 zero bytes is no key -- it is the sender a creation leaves blank. A
 * signature of zeros was never made, a uuid of zeros names no community. The question comes up
 * in the indices, for every key of every transaction they read, and in validation, for every
 * signature pair; it has one answer here.
 *
 * ### Why memcmp against zeros
 *
 * Measured, not assumed. With the length known at the call site, `memcmp` against an array of
 * zeros is expanded by the compiler into a few wide loads, or'ed together, with no loop and no
 * branch -- the same one or two nanoseconds whatever the bytes hold. A byte loop that stops at
 * the first nonzero byte is as fast for a real key and several times slower for a blank one,
 * because the compiler may not read past a byte the loop might stop at, and so reads one at a
 * time; blank senders are every creation, more than half of a real chain.
 *
 * The chunk is 32 bytes and not 64 for the same reason: on the baseline x86_64 target a
 * 64 byte `memcmp` is no longer expanded but becomes a call into the C library, about twice as
 * slow for a signature. Two 32 byte chunks stay inline, with one branch between them that a
 * real signature leaves at the first.
 *
 * Header only, and free of libsodium: an index that is built without the crypto module uses it
 * all the same.
 *
 * @{
 */

/** @brief Bytes one comparison covers; longer runs are compared a chunk at a time. */
#define GRDC_EMPTY_CHUNK_SIZE 32u

/**
 * @brief Whether every one of @p size bytes at @p bytes is zero.
 *
 * Compared in chunks of @ref GRDC_EMPTY_CHUNK_SIZE against a block of zeros. For a uuid or a
 * public key that is one comparison, for a signature two, and the compiler turns each into wide
 * loads.
 *
 * @param[in] bytes Where to look; not NULL unless @p size is 0.
 * @param[in] size  How many bytes; 0 is an empty run, and an empty run holds nothing but zeros.
 * @return true when all of them are zero.
 * @whisper Blank, as it was before anything was written
 */
static inline bool grdc_is_empty(const uint8_t *bytes, uint32_t size) {
  static const uint8_t zeros[GRDC_EMPTY_CHUNK_SIZE] = {0};
  // memcmp wants valid pointers even for a length of 0, so an empty run is answered before it
  if (!size) { return true; }
  while (size > GRDC_EMPTY_CHUNK_SIZE) {
    if (0 != memcmp(bytes, zeros, GRDC_EMPTY_CHUNK_SIZE)) { return false; }
    bytes += GRDC_EMPTY_CHUNK_SIZE;
    size -= GRDC_EMPTY_CHUNK_SIZE;
  }
  return 0 == memcmp(bytes, zeros, size);
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_CRYPTO_VALIDATION_H
