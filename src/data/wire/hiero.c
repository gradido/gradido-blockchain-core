#include "gradido_blockchain_core/data/wire/hiero.h"
#include "arnm/converter.h"
#include "gradido_blockchain_core/data/timestamp.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/utils/converter.h"

int64_t grdw_hiero_account_id_get_shared_num(const grdw_hiero_account_id *hiero_account_id) {
  if (!hiero_account_id) { return 0; }
  return hiero_account_id->shardNum;
}

int64_t grdw_hiero_account_id_get_realm_num(const grdw_hiero_account_id *hiero_account_id) {
  if (!hiero_account_id) { return 0; }
  return hiero_account_id->realmNum;
}

int64_t grdw_hiero_account_id_get_account_num(const grdw_hiero_account_id *hiero_account_id) {
  if (!hiero_account_id) { return 0; }
  return hiero_account_id->accountNum;
}

size_t grdw_hiero_account_id_calculate_string_size(const grdw_hiero_account_id *hiero_account_id) {
  if (!hiero_account_id) { return 0; }
  return arnm_int64_to_string_size(hiero_account_id->shardNum) +
         arnm_int64_to_string_size(hiero_account_id->realmNum) +
         arnm_int64_to_string_size(hiero_account_id->accountNum) + 2;
}
size_t grdw_hiero_account_id_to_string(
    char *buffer, size_t buffer_size, const grdw_hiero_account_id *hiero_account_id
) {
  if (!buffer || !buffer_size || !hiero_account_id) { return 0; }

  size_t shardNum_size = arnm_int64_to_string_size(hiero_account_id->shardNum);
  size_t realmNum_size = arnm_int64_to_string_size(hiero_account_id->realmNum);
  size_t accountNum_size = arnm_int64_to_string_size(hiero_account_id->accountNum);
  size_t result_size = shardNum_size + realmNum_size + accountNum_size + 2;
  // buffer_size counts the terminator; the last of the three numbers writes one.
  if (buffer_size < result_size + 1) { return result_size; }

  arnm_int64_to_string_known_string_size(buffer, hiero_account_id->shardNum, shardNum_size);
  buffer += shardNum_size;
  *buffer = '.';
  buffer++;
  arnm_int64_to_string_known_string_size(buffer, hiero_account_id->realmNum, realmNum_size);
  buffer += realmNum_size;
  *buffer = '.';
  buffer++;
  arnm_int64_to_string_known_string_size(buffer, hiero_account_id->accountNum, accountNum_size);
  return result_size;
}

const grdd_timestamp *grdw_hiero_transaction_id_get_transaction_valid_start(
    const grdw_hiero_transaction_id *hiero_transaction_id
) {
  if (!hiero_transaction_id) { return NULL; }
  return &hiero_transaction_id->transactionValidStart;
}

const grdw_hiero_account_id *grdw_hiero_transaction_id_get_account_id(
    const grdw_hiero_transaction_id *hiero_transaction_id
) {
  if (!hiero_transaction_id) { return NULL; }
  return &hiero_transaction_id->accountID;
}

size_t grdw_hiero_transaction_id_calculate_string_size(
    const grdw_hiero_transaction_id *hiero_transaction_id
) {
  if (!hiero_transaction_id) { return 0; }

  // 0 from the timestamp means it cannot be printed at all, and adding it to the account id's
  // length would answer with a number that measures nothing -- a caller would size a buffer from
  // it and then meet the 0 that grdw_hiero_transaction_id_to_string() answers with. The two say
  // the same thing about the same value.
  size_t timestamp_size =
      grdd_timestamp_calculate_string_size(&hiero_transaction_id->transactionValidStart);
  if (!timestamp_size) { return 0; }

  // the '@' between the two parts
  return grdw_hiero_account_id_calculate_string_size(&hiero_transaction_id->accountID) + 1 +
         timestamp_size;
}

size_t grdw_hiero_transaction_id_to_string(
    char *buffer, size_t buffer_size, const grdw_hiero_transaction_id *hiero_transaction_id
) {
  if (!buffer || !buffer_size || !hiero_transaction_id) { return 0; }

  // Measured once for the whole line instead of piece by piece. Each part closes its own run
  // with a terminator and the next part writes over it, so a per part check would have to reason
  // about a byte that is about to be overwritten -- and the '@' between them would land in the
  // one byte the account id thought was its own. Asking up front keeps that out of the writes.
  size_t timestamp_size =
      grdd_timestamp_calculate_string_size(&hiero_transaction_id->transactionValidStart);
  // 0 means the timestamp cannot be printed at all. Writing the account id and the '@' anyway
  // would leave the buffer without a terminator, since the '@' takes the one the account id set.
  if (!timestamp_size) { return 0; }

  size_t account_id_size =
      grdw_hiero_account_id_calculate_string_size(&hiero_transaction_id->accountID);
  size_t result_size = account_id_size + 1 + timestamp_size;
  if (buffer_size < result_size + 1) { return result_size; }

  grdw_hiero_account_id_to_string(buffer, buffer_size, &hiero_transaction_id->accountID);
  buffer += account_id_size;
  *buffer = '@';
  buffer++;
  grdd_timestamp_to_string(
      buffer, buffer_size - account_id_size - 1, &hiero_transaction_id->transactionValidStart
  );

  return result_size;
}
