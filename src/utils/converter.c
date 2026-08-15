#include "gradido_blockchain_core/utils/converter.h"
#include "hostmem/memory.h"
#include "hostmem/result.h"
#include <stdint.h>

#ifdef USE_SODIUM
#include "sodium.h"
#endif // USE_SODIUM

#include <assert.h>
#include <string.h>
#ifdef USE_SODIUM

/*
 * C11 static assert fallback safety
 */
#if !defined(static_assert)
#define static_assert _Static_assert
#endif

static_assert(UUID_BINARY_SIZE == 16, "uuid binary size don't match 16 bytes");

void grdu_uuid_to_string(char *result_buffer, const uint8_t uuid[UUID_BINARY_SIZE]) {
  char hex[33];
  sodium_bin2hex(hex, sizeof(hex), uuid, 16);
  memcpy(result_buffer, hex, 8);
  result_buffer[8] = '-';
  memcpy(result_buffer + 9, hex + 8, 4);
  result_buffer[13] = '-';
  memcpy(result_buffer + 14, hex + 12, 4);
  result_buffer[18] = '-';
  memcpy(result_buffer + 19, hex + 16, 4);
  result_buffer[23] = '-';
  memcpy(result_buffer + 24, hex + 20, 12);
  result_buffer[36] = '\0';
}

/*
hostmem_result grdu_uuid_from_string(uint8_t *uuid, const char *uuid_string) {
  if (!uuid || !uuid_string) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (strlen(uuid_string) != 36) { return HOSTMEM_ERROR_INVALID_PARAM; }

  char hex[33];
  memcpy(hex, uuid_string, 8);
  memcpy(hex + 8, uuid_string + 9, 4);
  memcpy(hex + 12, uuid_string + 14, 4);
  memcpy(hex + 16, uuid_string + 19, 4);
  memcpy(hex + 20, uuid_string + 24, 12);
  hex[32] = '\0';

  size_t bin_len = 0;
  if (sodium_hex2bin(uuid, 16, hex, 32, NULL, &bin_len, NULL) != 0) {
    return HOSTMEM_ERROR_ENCODE_FAILED;
  }
  if (bin_len != 16) { return HOSTMEM_ERROR_INVALID_PARAM; }
  return HOSTMEM_SUCCESS;
}
*/
// faster as version above
hostmem_result grdu_uuid_from_string(uint8_t *uuid, const char *uuid_string) {
  if (!uuid || !uuid_string) return HOSTMEM_ERROR_NULL_POINTER;
  if (strlen(uuid_string) != 36) return HOSTMEM_ERROR_INVALID_PARAM;

  static const uint8_t hex_lookup[256] = {
      ['0'] = 0,  ['1'] = 1,  ['2'] = 2,  ['3'] = 3,  ['4'] = 4,  ['5'] = 5,
      ['6'] = 6,  ['7'] = 7,  ['8'] = 8,  ['9'] = 9,  ['a'] = 10, ['b'] = 11,
      ['c'] = 12, ['d'] = 13, ['e'] = 14, ['f'] = 15, ['A'] = 10, ['B'] = 11,
      ['C'] = 12, ['D'] = 13, ['E'] = 14, ['F'] = 15,
  };

  size_t j = 0;
  for (size_t i = 0; i < 36; i++) {
    if (uuid_string[i] == '-') continue;
    uint8_t hi = hex_lookup[(unsigned char)uuid_string[i]];
    if (hi == 0 && uuid_string[i] != '0') { return HOSTMEM_ERROR_DECODE_FAILED; }
    ++i;
    uint8_t lo = hex_lookup[(unsigned char)uuid_string[i]];
    if (lo == 0 && uuid_string[i] != '0') { return HOSTMEM_ERROR_DECODE_FAILED; }
    if (hi == 0 && lo == 0 && uuid_string[i - 1] != '0') continue;
    uuid[j++] = (hi << 4) | lo;
  }

  return HOSTMEM_SUCCESS;
}

hostmem_result grdu_binary_to_hex(char *result_buffer, const hostmem_memory_block *data) {
  if (!result_buffer || !data || !data->size) { return HOSTMEM_ERROR_NULL_POINTER; }
  size_t hex_size = data->size * 2 + 1;

  sodium_bin2hex((char *)result_buffer, hex_size, data->data, data->size);
  return HOSTMEM_SUCCESS;
}

hostmem_result grdu_binary_from_hex(uint8_t *result_buffer, const char *hex) {
  if (!result_buffer || !hex) { return HOSTMEM_ERROR_NULL_POINTER; }
  size_t hex_size = strlen(hex);
  size_t bin_size = hex_size / 2;
  // invalid hex if size isn't power of 2
  if (bin_size * 2 != hex_size) { return HOSTMEM_ERROR_INVALID_PARAM; }
  size_t result_bin_size = 0;
  if (0 != sodium_hex2bin(result_buffer, bin_size, hex, hex_size, NULL, &result_bin_size, NULL)) {
    return HOSTMEM_ERROR_DECODE_FAILED;
  }
  if (result_bin_size != bin_size) { return HOSTMEM_ERROR_INVALID_STATE; }
  return HOSTMEM_SUCCESS;
}

const static int BASE64_VARIANT = sodium_base64_VARIANT_ORIGINAL;

size_t grdu_binary_to_base64_length(size_t binSize) {
  return sodium_base64_encoded_len(binSize, BASE64_VARIANT);
}

hostmem_result grdu_binary_to_base64_with_known_size(
    hostmem_memory_block *result_block, const hostmem_memory_block *data
) {
  if (!result_block || !data) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (NULL ==
      sodium_bin2base64(
          (char *)result_block->data, result_block->size, data->data, data->size, BASE64_VARIANT
      )) {
    return HOSTMEM_ERROR_OUT_OF_MEMORY;
  }
  return HOSTMEM_SUCCESS;
}

hostmem_result grdu_binary_to_base64(
    hostmem_memory_block *result_block, const hostmem_memory_block *data, hostmem *allocator
) {
  if (!result_block || !data || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  size_t strSize = sodium_base64_encoded_len(data->size, BASE64_VARIANT);
  if (strSize > UINT32_MAX - 7) { return HOSTMEM_ERROR_ARITHMETIC_OVERFLOW; }
  hostmem_result result = hostmem_memory_block_alloc(result_block, (uint32_t)strSize, allocator);
  if (result != HOSTMEM_SUCCESS) { return result; }

  return grdu_binary_to_base64_with_known_size(result_block, data);
}

size_t grdu_binary_from_base64(hostmem_memory_block *result_block, const char *base64_str) {
  if (!result_block || !base64_str) { return 0; }
  size_t result_bin_size = 0;
  if (sodium_base642bin(
          result_block->data, result_block->size, base64_str, strlen(base64_str), NULL,
          &result_bin_size, NULL, BASE64_VARIANT
      )) {
    return 0;
  }
  return result_bin_size;
}

#endif // USE_SODIUM
