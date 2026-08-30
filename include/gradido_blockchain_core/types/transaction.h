#ifndef GRADIDO_BLOCKCHAIN_CORE_TYPES_TRANSACTION_H
#define GRADIDO_BLOCKCHAIN_CORE_TYPES_TRANSACTION_H

#include "gradido_blockchain_core/types/memo_key.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \addtogroup enums
 * @{
 */

/*!
 * \enum grdt_transaction
 * Enum for different transaction types
 * !!! don't change order
 */
typedef enum grdt_transaction {
  //! Invalid or Empty Transaction
  GRDT_TRANSACTION_NONE = 0,

  //! Creation Transaction, creates new Gradidos
  GRDT_TRANSACTION_CREATION,

  //! Transfer Transaction, move Gradidos from one account to another
  GRDT_TRANSACTION_TRANSFER,

  //! Group Friends Update Transaction, update relationship between groups
  GRDT_TRANSACTION_COMMUNITY_FRIENDS_UPDATE,

  //! Register new address or sub address to group or move address to another group
  GRDT_TRANSACTION_REGISTER_ADDRESS,

  //! Special Transfer Transaction with timeout used for Gradido Link
  GRDT_TRANSACTION_DEFERRED_TRANSFER,

  //! First Transaction in Blockchain
  GRDT_TRANSACTION_COMMUNITY_ROOT,

  //! Redeeming deferred transfer
  GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER,

  //! Timeout deferred transfer, send back locked gdds
  GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER,

  //! Technical type for using it in for loops, as max index
  GRDT_TRANSACTION_COUNT
} grdt_transaction;

const char *grdt_transaction_to_string(grdt_transaction transaction);
grdt_transaction grdt_transaction_from_string(const char *transaction_string, size_t string_size);

/*! @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_TYPES_TRANSACTION_H
