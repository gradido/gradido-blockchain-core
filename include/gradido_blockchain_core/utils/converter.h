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
 * @brief Binary to text conversions that rest on libsodium.
 *
 * The plain number conversions moved to hostmem (@c hostmem/converter.h); what is left
 * here needs a crypto library and stays with the project that already links one. Every
 * function writes into a buffer the caller sized — none of them allocates.
 *
 * @{
 */

#ifdef USE_SODIUM

/**
 * @param[out] result_buffer expected to be 37 bytes for string uuid format with \0
 */
void grdu_uuid_to_string(char *result_buffer, const uint8_t uuid[UUID_BINARY_SIZE]);

/**
 * @param [out] uuid expect to be 16 bytes for uuid in binary representation
 * @param [in] uuid_string expect to be exactly 37 (36 + \0) bytes long
 */
hostmem_result grdu_uuid_from_string(uint8_t *uuid, const char *uuid_string);

/**
 * @brief Write @p data as lowercase hex into a buffer the caller sized.
 *
 * @param[out] result_buffer Expected to hold data->size * 2 + 1 bytes. Not checkable from
 *                           here — a short buffer aborts inside libsodium.
 * @param[in]  data          Block to encode; not NULL and not empty.
 * @retval HOSTMEM_SUCCESS             Hex written, terminator included.
 * @retval HOSTMEM_ERROR_NULL_POINTER  @p result_buffer, @p data or its data pointer is NULL.
 * @retval HOSTMEM_ERROR_INVALID_PARAM @p data holds no bytes.
 */
hostmem_result grdu_binary_to_hex(char *result_buffer, const hostmem_memory_block *data);

/**
 * @param result_buffer[out] expected to be strlen(hex) / 2
 * @param hex[in] expected to be null terminated string
 */
hostmem_result grdu_binary_from_hex(uint8_t *result_buffer, const char *hex);

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
