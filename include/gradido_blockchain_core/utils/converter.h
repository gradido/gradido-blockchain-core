#ifndef GRADIDO_BLOCKCHAIN_CORE_UTILS_CONVERTER_H
#define GRADIDO_BLOCKCHAIN_CORE_UTILS_CONVERTER_H

#include "gradido_blockchain_core/const.h"

#include "hostmem/memory_block.h"
#include "hostmem/result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup utils Utilities */

/**
 * @defgroup grdu_converter grdu_converter
 * @ingroup utils
 * @brief Binary to text conversions, most of them resting on libsodium.
 *
 * The plain number conversions moved to hostmem (@c hostmem/converter.h); what is left
 * here needs a crypto library and stays with the project that already links one. Every
 * function writes into a buffer the caller sized — none of them allocates.
 *
 * The uuid and hex functions are declared unconditionally: they work off lookup tables rather
 * than libsodium, so a build without @c USE_SODIUM still has them. Only the base64 group is
 * declared under that macro, and calling one of those from a build that does not define it is
 * a compile error rather than a link error.
 *
 * @warning None of these run in constant time, so none of them belong on secret material.
 * That is not a matter of how they are written: grdu_binary_to_hex() computes each digit
 * instead of looking it up, and in an optimised build its vectorised body really is branchless
 * — but the scalar path beside it, which takes the remainder and takes short inputs whole,
 * compiles to a compare and a jump on the nibble. Rewriting the conditional as an arithmetic
 * mask does not move it; the compiler turns that back into a branch as well, and an
 * unoptimised build has no vector path at all. The uuid pair reads lookup tables on top of
 * that. Hex that has to hide what it is carrying belongs in @c sodium_bin2hex /
 * @c sodium_hex2bin, which libsodium writes, tests and reviews for exactly this — and even
 * then the surrounding code still owes the secret its own wiping and locking.
 *
 * @{
 */

/**
 * @brief Parse the canonical 8-4-4-4-12 form into 16 bytes.
 *
 * @param [out] uuid        Expect to be 16 bytes for uuid in binary representation. Set to all
 *                          zeros when the string does not parse, so a caller that overlooks
 *                          the result code never reads half decoded bytes.
 * @param [in] uuid_string  Expect to be exactly 37 (36 + \0) bytes long, with the separators
 *                          at index 8, 13, 18 and 23. Both digit cases are accepted.
 * @retval HOSTMEM_SUCCESS             16 bytes written.
 * @retval HOSTMEM_ERROR_NULL_POINTER  @p uuid or @p uuid_string is NULL.
 * @retval HOSTMEM_ERROR_INVALID_PARAM @p uuid_string is not 36 characters long.
 * @retval HOSTMEM_ERROR_DECODE_FAILED A separator is missing or misplaced, or a character
 *                                     where a hex digit belongs is not one.
 * @note Available in every build; most of this group needs @c USE_SODIUM.
 */
hostmem_result grdu_uuid_from_string(uint8_t *uuid, const char *uuid_string);

/**
 * @param[out] result_buffer expected to be 37 bytes for string uuid format with \0
 * @note Available in every build; most of this group needs @c USE_SODIUM.
 */
void grdu_uuid_to_string(char *result_buffer, const uint8_t uuid[UUID_BINARY_SIZE]);

/**
 * @brief Write @p data as lowercase hex into a buffer the caller sized.
 *
 * @param[out] result_buffer Expected to hold data->size * 2 + 1 bytes. Not checkable from
 *                           here — sizing it is the caller's part of the contract.
 * @param[in]  data          Block to encode; not NULL and not empty.
 * @retval HOSTMEM_SUCCESS             Hex written, terminator included.
 * @retval HOSTMEM_ERROR_NULL_POINTER  @p result_buffer, @p data or its data pointer is NULL.
 * @retval HOSTMEM_ERROR_INVALID_PARAM @p data holds no bytes.
 */
hostmem_result grdu_binary_to_hex(char *result_buffer, const hostmem_memory_block *data);

/**
 * @brief Read a hex string back into the bytes it spells.
 *
 * Both digit cases are accepted. Nothing is skipped: a separator between the bytes makes the
 * string undecodable rather than being ignored.
 *
 * @param[out] result_buffer Expected to hold strlen(hex) / 2 bytes. Set to all zeros when the
 *                           string does not decode, so a caller that overlooks the result code
 *                           never reads half converted bytes.
 * @param[in]  hex           Null terminated string of an even number of hex digits. Empty is
 *                           allowed and writes nothing.
 * @retval HOSTMEM_SUCCESS             strlen(hex) / 2 bytes written.
 * @retval HOSTMEM_ERROR_NULL_POINTER  @p result_buffer or @p hex is NULL.
 * @retval HOSTMEM_ERROR_INVALID_PARAM @p hex has an odd number of characters.
 * @retval HOSTMEM_ERROR_DECODE_FAILED @p hex holds a character that is not a hex digit.
 */
hostmem_result grdu_binary_from_hex(uint8_t *result_buffer, const char *hex);

#ifdef USE_SODIUM

/**
 * @brief grdu_binary_to_hex() for material that has to keep quiet about itself.
 *
 * Same arguments, same result codes, libsodium underneath. Its bin2hex is written so that no
 * branch and no memory access follows the value being converted, and it is tested and reviewed
 * for holding that — which is the part grdu_binary_to_hex() cannot promise, however it is
 * written. Roughly twice as slow: 12.2 ns against 5.6 for 32 bytes in a ReleaseFast build.
 *
 * Reach for this when the bytes are a key, a seed or a passphrase. For hashes, transaction ids,
 * public keys and anything already public, grdu_binary_to_hex() is the one to use.
 *
 * @see grdu_binary_to_hex
 * @note Constant time here covers this conversion only. Wiping the caller's buffers afterwards
 *       and keeping them out of swap remains the caller's part.
 */
hostmem_result grdu_secret_to_hex(char *result_buffer, const hostmem_memory_block *data);

/**
 * @brief grdu_binary_from_hex() for material that has to keep quiet about itself.
 *
 * Same arguments, same result codes, libsodium underneath, and the output is wiped with
 * sodium_memzero() rather than memset() when the string does not decode — a clearing whose
 * result nobody reads is the kind a compiler may drop, and here it must not. The wider of the
 * two gaps: 91.2 ns against 9.1 for 32 bytes in a ReleaseFast build, because sodium_hex2bin
 * carries a state machine that cannot be vectorised at all.
 *
 * @see grdu_binary_from_hex
 * @note Constant time here covers this conversion only. Wiping the caller's buffers afterwards
 *       and keeping them out of swap remains the caller's part.
 */
hostmem_result grdu_secret_from_hex(uint8_t *result_buffer, const char *hex);

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
 * @retval HOSTMEM_SUCCESS                        Base64 written.
 * @retval HOSTMEM_ERROR_NULL_POINTER             A pointer, or a block's data pointer, is NULL.
 * @retval HOSTMEM_ERROR_DESTINATION_BUFFER_TO_SMALL @p result_block cannot hold the string.
 */
hostmem_result grdu_binary_to_base64_with_known_size(
    hostmem_memory_block *result_block, const hostmem_memory_block *data
);

/**
 * will reserve memory through allocator
 */
hostmem_result grdu_binary_to_base64(
    hostmem_memory_block *result_block, const hostmem_memory_block *data, hostmem *allocator
);

/**
 * @param result_buffer[out] expected to be (strlen(base64_str) / 4) * 3
 * @param base64_str[in] expected to be null terminated string
 * @return actual binary size or 0 on error
 */
size_t grdu_binary_from_base64(hostmem_memory_block *result_block, const char *base64_str);

#endif // USE_SODIUM
/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_UTILS_CONVERTER_H
