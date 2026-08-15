#ifdef USE_SODIUM

#include "utils.h"
#include "gradido_blockchain_core/memory.h"

#include "sodium.h"

#include <cstring>

grdu_memory_block fromBase64(
    const char *base64String, size_t size, int variant /* = sodium_base64_VARIANT_ORIGINAL  */
) {
  grdu_memory_block result{};
  size_t binSize = (size / 4) * 3;

  uint8_t *buffer = (uint8_t *)malloc(binSize);
  if (!buffer) { return result; }
  size_t resultBinSize = 0;
  const char *firstInvalidByte = nullptr;
  auto convertResult = sodium_base642bin(
      buffer, binSize, base64String, size, nullptr, &resultBinSize, &firstInvalidByte, variant
  );
  if (0 != convertResult) {
    printf("invalid base64: error at: %lld\n", firstInvalidByte - base64String);
  }
  if (resultBinSize < binSize) {
    result.data = (uint8_t *)malloc(resultBinSize);
    if (!result.data) { return result; }
    memcpy(result.data, buffer, resultBinSize);
    free(buffer);
  } else {
    result.data = buffer;
  }
  result.size = resultBinSize;
  return result;
}

std::string toBase64(grdu_memory_block *data, int variant /* = sodium_base64_VARIANT_ORIGINAL  */) {
  if (!data || !data->size) { return ""; }
  size_t encodedSize = sodium_base64_encoded_len(data->size, variant);
  uint8_t *buffer = (uint8_t *)malloc(encodedSize);
  if (!buffer) { return ""; }
  if (nullptr == sodium_bin2base64((char *)buffer, encodedSize, data->data, data->size, variant)) {
    free(buffer);
    return "";
  }
  std::string base64String((const char *)buffer, encodedSize - 1);
  free(buffer);
  return base64String;
}

std::string toHex(uint8_t publicKey[32]) {
  uint8_t buffer[65];
  sodium_bin2hex((char *)buffer, 65, publicKey, 32);
  return std::string((char *)buffer, 64);
}

std::string toHex(uint8_t *data, size_t size) {
  if (!data || !size) { return ""; }
  size_t hexSize = size * 2 + 1;
  uint8_t *buffer = (uint8_t *)malloc(hexSize);
  if (!buffer) { return ""; }
  sodium_bin2hex((char *)buffer, hexSize, data, size);
  std::string hex((char *)buffer, hexSize - 1);
  free(buffer);
  return hex;
}

std::vector<uint8_t> fromHex(const char *hex) {
  std::vector<uint8_t> result;
  if (!hex) { return result; }
  size_t hex_size = strlen(hex);
  size_t binSize = hex_size / 2;
  if (binSize * 2 != hex_size) {
    printf("invalid hex size\n");
    return result;
  }
  result.reserve(binSize);
  size_t resultBinSize = 0;
  if (0 !=
      sodium_hex2bin(result.data(), binSize, hex, hex_size, nullptr, &resultBinSize, nullptr)) {
    printf("invalid hex: %s\n", hex);
  }
  return result;
}

#endif // USE_SODIUM
