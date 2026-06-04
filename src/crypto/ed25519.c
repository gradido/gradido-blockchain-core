#include "gradido_blockchain_core/crypto/ed25519.h"
// make sure sodium expected key and seed size wasn't changed
#include "gradido_blockchain_core/crypto/ed25519_sodium_compat.h"
#include "gradido_blockchain_core/result.h"

#include "sodium.h"

#include <string.h>

void grdc_ed25519_key_pair_init(grdc_ed25519_key_pair* ed25519_key_pair)
{
  if (!ed25519_key_pair) { return; }
  memset(ed25519_key_pair, 0, sizeof(ed25519_key_pair));
}

grd_result grdc_ed25519_key_pair_generate_from_seed(grdc_ed25519_key_pair* ed25519_key_pair, const uint8_t seed[ED25519_SEED_SIZE])
{
  if (!ed25519_key_pair || !seed) { return GRD_ERROR_NULL_POINTER; }
  const uint8_t curveId[] = "ed25519 seed";
	uint8_t I[64];
	uint8_t temp[32];

	crypto_auth_hmacsha512_state state;
	crypto_auth_hmacsha512_init(&state, curveId, sizeof(curveId) - 1);
	crypto_auth_hmacsha512_update(&state, seed, ED25519_SEED_SIZE);
	crypto_auth_hmacsha512_final(&state, I);

	crypto_sign_seed_keypair(temp, ed25519_key_pair->private_key, I);
	memcpy(ed25519_key_pair->chain_code, &I[32], ED25519_CHAIN_CODE_SIZE);
	return GRD_SUCCESS;
}
