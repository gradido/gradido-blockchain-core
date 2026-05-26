#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_CROSS_GROUP_PB_COMPAT_H
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_CROSS_GROUP_PB_COMPAT_H

#include "gradido_blockchain_core/data/cross_group_type.h"
#include "gradido_blockchain_core/data/proto/gradido/transaction_body.h"
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

/*
 * Ensure pbtools enum == internal enum mapping stays stable.
 *
 * IMPORTANT:
 * If ANY of these fail, protobuf generation or enum ordering changed.
 */

#pragma warning(push)
#pragma warning(disable : 5287)

// LOCAL
static_assert(
    (int)GRDD_CROSS_GROUP_TYPE_LOCAL == (int)proto_gradido_transaction_body_local_e,
    "CrossGroupType enum mismatch: LOCAL"
);

// INBOUND
static_assert(
    (int)GRDD_CROSS_GROUP_TYPE_INBOUND == (int)proto_gradido_transaction_body_inbound_e,
    "CrossGroupType enum mismatch: INBOUND"
);

// OUTBOUND
static_assert(
    (int)GRDD_CROSS_GROUP_TYPE_OUTBOUND == (int)proto_gradido_transaction_body_outbound_e,
    "CrossGroupType enum mismatch: OUTBOUND"
);

// CROSS
static_assert(
    (int)GRDD_CROSS_GROUP_TYPE_CROSS == (int)proto_gradido_transaction_body_cross_e,
    "CrossGroupType enum mismatch: CROSS"
);

#pragma warning(pop)

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_CROSS_GROUP_PB_COMPAT_H
