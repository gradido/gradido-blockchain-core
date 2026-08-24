#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_ADDRESS_PB_COMPAT_H
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_ADDRESS_PB_COMPAT_H

#include "gradido_blockchain_core/data/proto/gradido/register_address.h"
#include "gradido_blockchain_core/types/address.h"
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * C11 static assert fallback safety
 */
#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert _Static_assert
#endif

/*
 * Ensure pbtools enum == internal enum mapping stays stable.
 *
 * IMPORTANT:
 * If ANY of these fail, protobuf generation or enum ordering changed.
 */

// C5287 is MSVC's "operands are different enum types" on the static_asserts below, which is
// exactly what they are there to compare. gcc and clang have no such pragma and warn about the
// directive itself (-Wunknown-pragmas), so it is asked for only where it means something.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 5287)
#endif

// NONE
static_assert(
    (int)GRDT_ADDRESS_NONE == (int)proto_gradido_register_address_none_e,
    "AddressType enum mismatch: NONE"
);

// COMMUNITY_HUMAN
static_assert(
    (int)GRDT_ADDRESS_COMMUNITY_HUMAN == (int)proto_gradido_register_address_community_human_e,
    "AddressType enum mismatch: COMMUNITY_HUMAN"
);

// COMMUNITY_GMW
static_assert(
    (int)GRDT_ADDRESS_COMMUNITY_GMW == (int)proto_gradido_register_address_community_gmw_e,
    "AddressType enum mismatch: COMMUNITY_GMW"
);

// COMMUNITY_AUF
static_assert(
    (int)GRDT_ADDRESS_COMMUNITY_AUF == (int)proto_gradido_register_address_community_auf_e,
    "AddressType enum mismatch: COMMUNITY_AUF"
);

// COMMUNITY_PROJECT
static_assert(
    (int)GRDT_ADDRESS_COMMUNITY_PROJECT == (int)proto_gradido_register_address_community_project_e,
    "AddressType enum mismatch: COMMUNITY_PROJECT"
);

// SUBACCOUNT
static_assert(
    (int)GRDT_ADDRESS_SUBACCOUNT == (int)proto_gradido_register_address_subaccount_e,
    "AddressType enum mismatch: SUBACCOUNT"
);

// CRYPTO_ACCOUNT
static_assert(
    (int)GRDT_ADDRESS_CRYPTO_ACCOUNT == (int)proto_gradido_register_address_crypto_account_e,
    "AddressType enum mismatch: CRYPTO_ACCOUNT"
);

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_ADDRESS_PB_COMPAT_H
