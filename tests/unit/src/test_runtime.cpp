#include "../terminal_colors.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/data/timestamp.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"
#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/data/wire/hiero.h"
#include "gradido_blockchain_core/data/wire/ledger_anchor.h"
#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "gradido_blockchain_core/mapping/runtime_from_wire.h"
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/utils/mono_timer.h"
#include "gradido_blockchain_core/utils/version.h"
#include "key_pairs.h"

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

static grd_memory_block fromBase64(
    const char *base64String, size_t size, int variant = sodium_base64_VARIANT_ORIGINAL
) {
  grd_memory_block result{};
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
  grd_memory mem;
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);

  grdw_confirmed_transaction confirmed_tx;
  grdw_confirmed_transaction_init(&confirmed_tx);
  auto base64 = fromBase64(
      confirmedCommunityRootTransactionBase64, strlen(confirmedCommunityRootTransactionBase64)
  );
  ASSERT_EQ(grdw_confirmed_transaction_decode(&confirmed_tx, &base64, &mem), GRD_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  ASSERT_EQ(
      grdw_transaction_body_decode(&body, &confirmed_tx.transaction.body_bytes, &mem), GRD_SUCCESS
  );
  grdr_complete_transaction tx;
  ASSERT_EQ(
      grdm_complete_transaction_from_wire(&tx, &body, &confirmed_tx, community_uuid), GRD_SUCCESS
  );
}
