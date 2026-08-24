#ifndef GRADIDO_BLOCKCHAIN_CORE_UTILS_CONVERTER_H
#define GRADIDO_BLOCKCHAIN_CORE_UTILS_CONVERTER_H

#include "gradido_blockchain_core/const.h"

#include "arnm/memory_block.h"
#include "arnm/result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup utils Utilities */

/**
 * @defgroup grdu_converter grdu_converter
 * @ingroup utils
 * @brief Binary to text conversions that need a crypto library.
 *
 * The number, hex and uuid conversions all moved to arnm (@c arnm/converter.h). What is
 * left here is what arnm cannot carry, because it does not link libsodium: base64, and the
 * hex pair for material that has to keep quiet about itself. Every function writes into a
 * buffer the caller sized — none of them allocates.
 *
 * Everything in this group needs @c USE_SODIUM, so calling one from a build that does not
 * define it is a compile error rather than a link error.
 *
 * For hex on bytes that are already public — hashes, transaction ids, public keys — reach for
 * @c arnm_binary_to_hex / @c arnm_binary_from_hex instead. They are the faster pair and
 * are there in every build; the two here buy constant time and cost between two and ten times
 * the runtime for it. `bench_numberToString` prints both side by side, which is the reason
 * that comparison lives in this project and not in arnm.
 *
 * @{
 */

#ifdef USE_SODIUM

/**
 * @brief arnm_binary_to_hex() for material that has to keep quiet about itself.
 *
 * Same arguments, same result codes, libsodium underneath. Its bin2hex is written so that no
 * branch and no memory access follows the value being converted, and it is tested and reviewed
 * for holding that — which is the part arnm_binary_to_hex() cannot promise, however it
 * is written. Roughly twice as slow: about 12 ns against 6 for 32 bytes in a ReleaseFast build,
 * a ratio `bench_numberToString` reprints on whatever machine asks.
 *
 * Reach for this when the bytes are a key, a seed or a passphrase. For hashes, transaction ids,
 * public keys and anything already public, arnm_binary_to_hex() is the one to use.
 *
 * @see arnm_binary_to_hex
 * @note Constant time here covers this conversion only. Wiping the caller's buffers afterwards
 *       and keeping them out of swap remains the caller's part.
 */
arnm_result grdu_secret_to_hex(char *result_buffer, const arnm_memory_block *data);

/**
 * @brief arnm_binary_from_hex() for material that has to keep quiet about itself.
 *
 * Same arguments, same result codes, libsodium underneath. The wider of the two gaps: about
 * 94 ns against 9 for 32 bytes in a ReleaseFast build, because sodium_hex2bin carries a state
 * machine that cannot be vectorised at all.
 *
 * @param[out] result_buffer Expected to hold strlen(hex) / 2 bytes. Those bytes are wiped with
 *                           sodium_memzero() — not memset(), whose result nobody reads and which
 *                           a compiler may therefore drop — when the string turns out not to be
 *                           hex, so a caller that overlooks the result code cannot read half a
 *                           secret. What the buffer held before this call is not touched on that
 *                           path either way: only the bytes this call decoded are cleared.
 * @param[in]  hex           Null terminated string of an even number of hex digits.
 * @retval ARNM_SUCCESS             strlen(hex) / 2 bytes written.
 * @retval ARNM_ERROR_NULL_POINTER  @p result_buffer or @p hex is NULL.
 * @retval ARNM_ERROR_INVALID_PARAM @p hex has an odd number of characters. Refused before
 *                                     anything is written, so @p result_buffer is left exactly
 *                                     as the caller had it — there is nothing of this call's
 *                                     making in it to hide, and how much of it is even
 *                                     addressable is not knowable from here.
 * @retval ARNM_ERROR_DECODE_FAILED @p hex holds a character that is not a hex digit. The
 *                                     strlen(hex) / 2 bytes are zeroed.
 *
 * @see arnm_binary_from_hex
 * @note Constant time here covers this conversion only. Wiping the caller's own buffers, this
 *       one included once it has served its purpose, and keeping them out of swap remains the
 *       caller's part.
 */
arnm_result grdu_secret_from_hex(uint8_t *result_buffer, const char *hex);

/**
 * for precalculation of neccessary size
 */
size_t grdu_binary_to_base64_length(size_t binSize);

/**
 * @brief Write @p data as base64 into a block the caller reserved.
 *
 * Size the block with grdu_binary_to_base64_length() beforehand. The room is verified here
 * rather than trusted: libsodium answers a destination that is too small by aborting the
 * process, so the check has to happen before the call.
 *
 * @param[out] result_block Receives the string including its \0 terminator.
 * @param[in]  data         Block to encode; not NULL.
 * @retval ARNM_SUCCESS                        Base64 written.
 * @retval ARNM_ERROR_NULL_POINTER             A pointer, or a block's data pointer, is NULL.
 * @retval ARNM_ERROR_DESTINATION_BUFFER_TO_SMALL @p result_block cannot hold the string.
 */
arnm_result grdu_binary_to_base64_with_known_size(
    arnm_memory_block *result_block, const arnm_memory_block *data
);

/**
 * will reserve memory through allocator
 */
arnm_result grdu_binary_to_base64(
    arnm_memory_block *result_block, const arnm_memory_block *data, arnm *allocator
);

/**
 * @param result_buffer[out] expected to be (strlen(base64_str) / 4) * 3
 * @param base64_str[in] expected to be null terminated string
 * @return actual binary size or 0 on error
 */
size_t grdu_binary_from_base64(arnm_memory_block *result_block, const char *base64_str);

#endif // USE_SODIUM
/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_UTILS_CONVERTER_H
