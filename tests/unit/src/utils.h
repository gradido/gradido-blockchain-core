#include "gradido_blockchain_core/memory.h"

#include <string>
#include <stddef.h>
#include <vector>

#ifdef USE_SODIUM
#include "sodium.h"

struct grd_memory_block;

grd_memory_block fromBase64(
    const char *base64String, size_t size, int variant = sodium_base64_VARIANT_ORIGINAL
);
std::string toBase64(grd_memory_block *data, int variant /* = sodium_base64_VARIANT_ORIGINAL  */);
std::string toHex(uint8_t publicKey[32]);
std::string toHex(uint8_t* data, size_t size);
inline std::string toHex(grd_memory_block* data) {
  return toHex(data->data, data->size);
}
std::vector<uint8_t> fromHex(const char *hex);

#endif // USE_SODIUM
