#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_DERIVATION_PB_COMPAT_H
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_DERIVATION_PB_COMPAT_H

#include "gradido_blockchain_core/data/balance_derivation_type.h"
#include "gradido_blockchain_core/data/proto/gradido/ledger_metadata.h"
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
typedef enum {
  GRDW_BALANCE_DERIVATION_UNSPECIFIED = 0,
  GRDW_BALANCE_DERIVATION_NODE = 1,
  GRDW_BALANCE_DERIVATION_EXTERN = 2
} grdw_balance_derivation;

enum proto_gradido_balance_derivation_e {
    proto_gradido_unspecified_e = 0,
    proto_gradido_node_e = 1,
    proto_gradido_extern_e = 2
};
*/

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

// UNSPECIFIED
static_assert(
    GRDD_BALANCE_DERIVATION_UNSPECIFIED ==
        proto_gradido_balance_derivation_e::proto_gradido_unspecified_e,
    "BalanceDerivation enum mismatch: UNSPECIFIED"
);

// NODE
static_assert(
    GRDD_BALANCE_DERIVATION_NODE == proto_gradido_balance_derivation_e::proto_gradido_node_e,
    "BalanceDerivation enum mismatch: NODE"
);

// EXTERN
static_assert(
    GRDD_BALANCE_DERIVATION_EXTERN == proto_gradido_balance_derivation_e::proto_gradido_extern_e,
    "BalanceDerivation enum mismatch: EXTERN"
);

#pragma warning(pop)

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_DERIVATION_PB_COMPAT_H
