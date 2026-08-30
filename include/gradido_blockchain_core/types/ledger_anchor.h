#ifndef GRADIDO_BLOCKCHAIN_CORE_TYPES_LEDGER_ANCHOR_H
#define GRADIDO_BLOCKCHAIN_CORE_TYPES_LEDGER_ANCHOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GRDT_LEDGER_ANCHOR_UNSPECIFIED = 0,
  // 1 was used for Iota message id, but cannot be used any longer
  GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID = 2,
  GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_ID = 3,
  GRDT_LEDGER_ANCHOR_NODE_TRIGGER_TRANSACTION_ID = 4,
  GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_COMMUNITY_ID = 5,
  GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_USER_ID = 6,
  GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_CONTRIBUTION_ID = 7,
  GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_LINK_ID = 8
} grdt_ledger_anchor;

const char *grdt_ledger_anchor_to_string(grdt_ledger_anchor ledger_anchor);
grdt_ledger_anchor grdt_ledger_anchor_from_string(
    const char *ledger_anchor_string, size_t string_size
);

#ifdef __cplusplus
}
#endif

#endif /* GRADIDO_BLOCKCHAIN_CORE_TYPES_LEDGER_ANCHOR_H */
