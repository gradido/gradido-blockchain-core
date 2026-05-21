#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_RUNTIME_LEDGER_ANCHOR_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_RUNTIME_LEDGER_ANCHOR_H

#include "gradido_blockchain_core/data/timestamp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grdr_ledger_anchor {
    uint8_t type;

    union {
        struct {
            uint64_t consensus_seconds;
            uint64_t account_id_shard;
            uint64_t account_id_realm;
            uint64_t account_id_account;
        } hiero;

        uint64_t legacy_id;
        uint64_t node_trigger_id;
    };

} grdr_ledger_anchor;


#ifdef __cplusplus
}
#endif

#endif //GRADIDO_BLOCKCHAIN_CORE_DATA_RUNTIME_LEDGER_ANCHOR_H
