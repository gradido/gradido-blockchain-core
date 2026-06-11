#ifdef USE_SODIUM
#include "gradido_blockchain_core/crypto/hash.h"
#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/crypto/hash_sodium_compat.h"
#include "gradido_blockchain_core/memory.h"

#include "sodium.h"

grd_result grdc_generic_hash(uint8_t *hash, const grd_memory_block *data_block) {
  if (!hash || !data_block) { return GRD_ERROR_NULL_POINTER; }
  if (!data_block->size) { return GRD_ERROR_INVALID_PARAM; }
  crypto_generichash(hash, GENERIC_HASH_SIZE, data_block->data, data_block->size, NULL, 0);
  return GRD_SUCCESS;
}
#endif // USE_SODIUM
