#include "gradido_blockchain_core/data/timestamp.h"
#include "gradido_blockchain_core/data/wire/hiero.h"
#include "memory_limit.h"
#include <cstring>
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
  ASSERT_EQ(written, 28);
  EXPECT_STREQ(buffer, "0.0.1233@171627121.000002912");
}

// promise: both hiero conversions size their destination the way snprintf does -- buffer_size
// counts the terminator, and the return is the character count without it, so a caller adds one
// to what it was told. A buffer of exactly the character count is refused rather than filled,
// which is what keeps the terminator inside it.
TEST(HieroAccountIdTest, BufferSizeCountsTheTerminator) {
  grdw_hiero_account_id accountId = {.shardNum = 0, .realmNum = 0, .accountNum = 12121};
  const char *expected = "0.0.12121";
  const size_t characters = strlen(expected);
  ASSERT_EQ(grdw_hiero_account_id_calculate_string_size(&accountId), characters);

  char exact[16];
  memset(exact, 'x', sizeof(exact));
  ASSERT_EQ(grdw_hiero_account_id_to_string(exact, characters + 1, &accountId), characters);
  EXPECT_STREQ(exact, expected);
  EXPECT_EQ(exact[characters + 1], 'x') << "wrote past the buffer it was given";

  char one_short[16];
  memset(one_short, 'x', sizeof(one_short));
  EXPECT_EQ(grdw_hiero_account_id_to_string(one_short, characters, &accountId), characters);
  for (char c : one_short) { EXPECT_EQ(c, 'x') << "wrote into a buffer it had refused"; }
}

TEST(HieroTransactionIdTest, BufferSizeCountsTheTerminator) {
  grdw_hiero_transaction_id transactionId = {
      .transactionValidStart = {.seconds = 171627121, .nanos = 2912},
      .accountID = {.shardNum = 0, .realmNum = 0, .accountNum = 1233}
  };
  const char *expected = "0.0.1233@171627121.000002912";
  const size_t characters = strlen(expected);
  ASSERT_EQ(grdw_hiero_transaction_id_calculate_string_size(&transactionId), characters);

  char exact[40];
  memset(exact, 'x', sizeof(exact));
  ASSERT_EQ(grdw_hiero_transaction_id_to_string(exact, characters + 1, &transactionId), characters);
  EXPECT_STREQ(exact, expected);
  EXPECT_EQ(exact[characters + 1], 'x') << "wrote past the buffer it was given";

  char one_short[40];
  memset(one_short, 'x', sizeof(one_short));
  EXPECT_EQ(grdw_hiero_transaction_id_to_string(one_short, characters, &transactionId), characters);
  for (char c : one_short) { EXPECT_EQ(c, 'x') << "wrote into a buffer it had refused"; }
}

// promise: the size calculation and the writer agree about a value that cannot be printed. Nanos
// outside 0..999999999 are what a wire type may carry and a timestamp may not, and the pair used
// to disagree there -- the calculation added the account id's length to the timestamp's 0 and
// returned a figure that measured nothing, while the writer refused outright. A caller sizing a
// buffer from the first and then calling the second would have been told two different things.
TEST(HieroTransactionIdTest, UnprintableValidStartIsZeroFromBothSides) {
  for (int32_t nanos : {-1, 1000000000, INT32_MIN, INT32_MAX}) {
    grdw_hiero_transaction_id transactionId = {
        .transactionValidStart = {.seconds = 171627121, .nanos = nanos},
        .accountID = {.shardNum = 0, .realmNum = 0, .accountNum = 1233}
    };

    EXPECT_EQ(grdw_hiero_transaction_id_calculate_string_size(&transactionId), 0u)
        << "nanos " << nanos;

    char buffer[64];
    memset(buffer, 'x', sizeof(buffer));
    EXPECT_EQ(grdw_hiero_transaction_id_to_string(buffer, sizeof(buffer), &transactionId), 0u)
        << "nanos " << nanos;
    for (char c : buffer) { EXPECT_EQ(c, 'x') << "nanos " << nanos << ": wrote anyway"; }
  }

  EXPECT_EQ(grdw_hiero_transaction_id_calculate_string_size(nullptr), 0u);
  EXPECT_EQ(grdw_hiero_account_id_calculate_string_size(nullptr), 0u);
}
