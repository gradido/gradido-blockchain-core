#ifndef GRADIDO_BLOCKCHAIN_CORE_BLAKE2B_SODIUM_COMPAT_H
#define GRADIDO_BLOCKCHAIN_CORE_BLAKE2B_SODIUM_COMPAT_H

#include "gradido_blockchain_core/const.h"

#include "sodium.h"

#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * C11 static assert fallback safety
 */
#if !defined(static_assert)
#define static_assert _Static_assert
#endif

static_assert(
    BLAKE2B_HASH_SIZE == crypto_generichash_BYTES,
    "sodium generic hash size don't match blake2b hash size"
);

#pragma warning(pop)

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_BLAKE2B_SODIUM_COMPAT_H
