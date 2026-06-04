#ifndef GRADIDO_BLOCKCHAIN_CORE_CRYPTO_ED25519_H
#define GRADIDO_BLOCKCHAIN_CORE_CRYPTO_ED25519_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/result.h"

#include <stdint.h>

typedef struct grdc_ed25519_key_pair {
  union {
    struct {
      uint8_t seed[ED25519_SEED_SIZE];
      uint8_t public_key[ED25519_PUBLIC_KEY_SIZE];
    };
    uint8_t private_key[ED25519_PRIVATE_KEY_SIZE];
  };
  uint8_t chain_code[ED25519_CHAIN_CODE_SIZE];
} grdc_ed25519_key_pair;

void grdc_ed25519_key_pair_init(grdc_ed25519_key_pair* ed25519_key_pair);
// seed generation by slip0010
grd_result grdc_ed25519_key_pair_generate_from_seed(grdc_ed25519_key_pair* ed25519_key_pair, const uint8_t seed[ED25519_SEED_SIZE]);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_CRYPTO_ED25519_H
