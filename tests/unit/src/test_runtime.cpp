#include "../terminal_colors.h"
#include "arnm/arena.h"
#include "arnm/mono_timer.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/data/timestamp.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"
#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/data/wire/hiero.h"
#include "gradido_blockchain_core/data/wire/ledger_anchor.h"
#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "gradido_blockchain_core/mapping/json_from_runtime.h"
#include "gradido_blockchain_core/mapping/runtime_from_json.h"
#include "gradido_blockchain_core/mapping/runtime_from_wire.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/utils/version.h"
#include "key_pairs.h"

#include "memory_limit.h"
#include "sodium.h"
#include <assert.h>
#include <gtest/gtest.h>
#include <string.h>

// Confirmed Gradido Transaction
constexpr auto confirmedCommunityRootTransactionBase64 =
    "CHkS3gEKZgpkCiCBZwMplGmI7fRR9MQkaR2Dz1qQQ5BCiC1btyJD71Ue9BJAtT7yJ8kBub5BCxCDG5wZ8s/"
    "dFKf2ystCXQQc4lZnkZmffBwTO6Udq5LupPfaAbvyFIt7942U+"
    "kHiuF52wokQChJ0WmYKIIFnAymUaYjt9FH0xCRpHYPPWpBDkEKILVu3IkPvVR70EiDX46igkKpEhzJG9cas/Bf/"
    "dO4XT1bnvSpV/7gQQfbbHRogrYcHSiqkvALTeX+7Q8iyvm7dbLHWNEqUD8UovOHhMLISBgiAzLn/BRiIgAwaBgjC8rn/"
    "BSCIgAwqIGHF/azvYntEu9pwC3bmSL61/"
    "Ob0pLcCTWspFwaJb4Q7MhQaEAoKCIDMuf8FELeVERICGHkIAjo3CiDbDtYSWhTwMKvtG/"
    "yDHgohjPn6v87n7NWBwMDniPAXxxCQThoQAZ4sMaMDdcCUHvNcWeT5eEAC";

static const uint8_t community_uuid[16] = {0x01, 0x9e, 0x2c, 0x31, 0xa3, 0x03, 0x75, 0xc0,
                                           0x94, 0x1e, 0xf3, 0x5c, 0x59, 0xe4, 0xf9, 0x78};

static arnm_memory_block fromBase64(
    const char *base64String, size_t size, int variant = sodium_base64_VARIANT_ORIGINAL
) {
  arnm_memory_block result{};
  size_t binSize = (size / 4) * 3;

  uint8_t *buffer = (uint8_t *)malloc(binSize);
  if (!buffer) { return result; }
  size_t resultBinSize = 0;
  const char *firstInvalidByte = nullptr;
  auto convertResult = sodium_base642bin(
      buffer, binSize, base64String, size, nullptr, &resultBinSize, &firstInvalidByte, variant
  );
  if (0 != convertResult) {
    printf("invalid base64: error at: %lld\n", firstInvalidByte - base64String);
  }
  if (resultBinSize < binSize) {
    result.data = (uint8_t *)malloc(resultBinSize);
    if (!result.data) { return result; }
    memcpy(result.data, buffer, resultBinSize);
    free(buffer);
  } else {
    result.data = buffer;
  }
  result.size = resultBinSize;
  return result;
}

TEST(RuntimeTest, ConfirmedTransaction_Decode_ToRuntime_CommunityRoot) {
  arnm mem{};
  ASSERT_EQ(arnm_init_arena(&mem, 2048), ARNM_SUCCESS);

  grdw_confirmed_transaction confirmed_tx;
  grdw_confirmed_transaction_init(&confirmed_tx);
  auto base64 = fromBase64(
      confirmedCommunityRootTransactionBase64, strlen(confirmedCommunityRootTransactionBase64)
  );
  ASSERT_EQ(grdw_confirmed_transaction_decode(&confirmed_tx, &base64, &mem), ARNM_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  ASSERT_EQ(
      grdw_transaction_body_decode(&body, &confirmed_tx.transaction.body_bytes, &mem), ARNM_SUCCESS
  );
  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);
  ASSERT_EQ(
      grdm_complete_transaction_from_wire(&tx, &body, &confirmed_tx, community_uuid), ARNM_SUCCESS
  );

  // the runtime transaction carries an arena of its own, the wire decode borrowed this one, and
  // fromBase64 hands back malloc'd bytes -- three owners, three releases
  grdr_complete_transaction_release(&tx);
  free(base64.data);
  arnm_release(&mem);
}

TEST(RuntimeTest, ConfirmedTransaction_Decode_ToRuntime_ToJson_AndBack) {
  // one arena for the wire decode, the JSON document, the rendered text and the parse of it
  // again -- generous on purpose, so the test measures the mapping and not the ground it stands
  // on
  arnm mem{};
  ASSERT_EQ(arnm_init_arena(&mem, 64 * 1024), ARNM_SUCCESS);

  grdw_confirmed_transaction confirmed_tx;
  grdw_confirmed_transaction_init(&confirmed_tx);
  auto base64 = fromBase64(
      confirmedCommunityRootTransactionBase64, strlen(confirmedCommunityRootTransactionBase64)
  );
  ASSERT_EQ(grdw_confirmed_transaction_decode(&confirmed_tx, &base64, &mem), ARNM_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  ASSERT_EQ(
      grdw_transaction_body_decode(&body, &confirmed_tx.transaction.body_bytes, &mem), ARNM_SUCCESS
  );
  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);
  ASSERT_EQ(
      grdm_complete_transaction_from_wire(&tx, &body, &confirmed_tx, community_uuid), ARNM_SUCCESS
  );

  // the JSON pair is exercised in full on built fixtures in test_json; what is checked here is
  // that a transaction which really came off the wire survives the same passage -- the arrays,
  // the memos and the body bytes are the ones protobuf produced, not the ones a test chose
  arnm_memory_block text{};
  const auto written =
      grdm_json_from_complete_transaction(&text, &tx, &mem, ARNM_JSON_WRITE_DEFAULT);
  ASSERT_TRUE(ARNM_SUCCESS == written || ARNM_WARNING_ARENA_MEMORY_NOT_RECLAIMED == written)
      << grd_result_to_string(written);

  grdr_complete_transaction from_json;
  grdr_complete_transaction_init(&from_json);
  ASSERT_EQ(
      grdm_complete_transaction_from_json(
          &from_json, (const char *)text.data, text.size - 1, &mem, ARNM_JSON_READ_DEFAULT
      ),
      ARNM_SUCCESS
  );

  EXPECT_EQ(tx.tx_nr, from_json.tx_nr);
  EXPECT_EQ(tx.transaction_type, from_json.transaction_type);
  EXPECT_EQ(tx.balance_derivation_type, from_json.balance_derivation_type);
  EXPECT_EQ(tx.cross_group_type, from_json.cross_group_type);
  EXPECT_EQ(tx.confirmed_at.seconds, from_json.confirmed_at.seconds);
  EXPECT_EQ(tx.created_at.seconds, from_json.created_at.seconds);
  EXPECT_EQ(tx.ledger_anchor.type, from_json.ledger_anchor.type);
  EXPECT_EQ(0, memcmp(tx.tx_community_uuid, from_json.tx_community_uuid, ARNM_UUID_BINARY_SIZE));
  EXPECT_EQ(0, memcmp(tx.tx_running_hash, from_json.tx_running_hash, GENERIC_HASH_SIZE));
  EXPECT_EQ(
      0, memcmp(
             tx.community_root.public_key, from_json.community_root.public_key, SIGN_PUBLIC_KEY_SIZE
         )
  );
  EXPECT_EQ(tx.account_balances_count, from_json.account_balances_count);
  EXPECT_EQ(tx.encrypted_memos_count, from_json.encrypted_memos_count);
  EXPECT_EQ(tx.signature_pairs_count, from_json.signature_pairs_count);
  ASSERT_EQ(tx.body_bytes.size, from_json.body_bytes.size);
  EXPECT_EQ(0, memcmp(tx.body_bytes.data, from_json.body_bytes.data, tx.body_bytes.size));

  grdr_complete_transaction_release(&from_json);
  grdr_complete_transaction_release(&tx);
  free(base64.data);
  arnm_release(&mem);
}
