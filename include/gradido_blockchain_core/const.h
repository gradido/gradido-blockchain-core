#ifndef GRADIDO_BLOCKCHAIN_CORE_CONST_H
#define GRADIDO_BLOCKCHAIN_CORE_CONST_H

#include "hostmem/converter.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIGN_PUBLIC_KEY_SIZE 32
#define SIGN_SEED_SIZE 32
/* Seed length a master key may be derived from, in bytes. The bounds are SLIP-10 and BIP-32:
   128 to 512 bits of entropy. Shorter is refused rather than quietly accepted, because the
   derived key would carry less entropy than its own length suggests. */
#define SIGN_SEED_MIN_SIZE 16
#define SIGN_SEED_MAX_SIZE 64
#define SIGN_CHAIN_CODE_SIZE 32
#define SIGN_PRIVATE_KEY_SIZE 64
#define SIGN_SIGNATURE_SIZE 64
#define GENERIC_HASH_SIZE 32
/* A uuid's 16 bytes are hostmem's HOSTMEM_UUID_BINARY_SIZE; it is not repeated here, so the two
   cannot drift apart. grdc_uuid_binary_size() below hands it to callers that reach this library
   through ffi and have no headers to read it from. */
const static int MAGIC_NUMBER_MAX_TIMESPAN_BETWEEN_CREATING_AND_RECEIVING_TRANSACTION_SECONDS = 120;
const static int64_t GRADIDO_DECAY_RESPITE_CENT = 100;

// for ffi
int grdc_sign_public_key_size();
int grdc_sign_seed_size();
int grdc_sign_chain_code_size();
int grdc_sign_private_key_size();
int grdc_sign_signature_size();
int grdc_generic_hash_size();
int grdc_uuid_binary_size();
int64_t grdc_decay_respite_cent();

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_CONST_H
