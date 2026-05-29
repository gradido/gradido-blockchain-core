#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/memory.h"
#include <string.h>

void grdr_complete_transaction_init(grdr_complete_transaction *tx) {
  if (tx) { memset(tx, 0, sizeof(grdr_complete_transaction)); }
}

void grdr_complete_transaction_release(grdr_complete_transaction *tx) {
  if (tx) {
    grd_memory_free(&tx->memory_area);
    grdr_complete_transaction_init(tx);
  }
}
