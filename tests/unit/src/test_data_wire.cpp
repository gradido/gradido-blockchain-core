#include "gradido_blockchain_core/data/timestamp.h"
#include "gradido_blockchain_core/data/wire/hiero.h"
#include <gtest/gtest.h>

TEST(HieroAccountIdTest, toString) {
  grdw_hiero_account_id accountId = {.shardNum = 0, .realmNum = 0, .accountNum = 12121};
  char buffer[128];
  size_t written = grdw_hiero_account_id_to_string(buffer, 128, &accountId);
  ASSERT_EQ(written, 9);
  EXPECT_STREQ(buffer, "0.0.12121");

  accountId = {.shardNum = 0, .realmNum = -2, .accountNum = 12132};
  written = grdw_hiero_account_id_to_string(buffer, 128, &accountId);
  ASSERT_EQ(written, 10);
  EXPECT_STREQ(buffer, "0.-2.12132");
}

TEST(HieroTransactionIdTest, toString) {
  grdw_hiero_transaction_id transactionId = {
      .transactionValidStart = {.seconds = 171627121, .nanos = 2912},
      .accountID = {.shardNum = 0, .realmNum = 0, .accountNum = 1233}
  };

  char buffer[128];
  size_t written = grdw_hiero_transaction_id_to_string(buffer, 128, &transactionId);
  ASSERT_EQ(written, 23);
  EXPECT_STREQ(buffer, "0.0.1233@171627121.2912");
}
