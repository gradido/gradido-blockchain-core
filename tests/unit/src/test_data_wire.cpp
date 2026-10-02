#include "arnm/arena.h"
#include "arnm/converter.h"
#include "gradido_blockchain_core/data/timestamp.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"
#include "gradido_blockchain_core/data/wire/hiero.h"
#include "memory_limit.h"
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <initializer_list>
#include <vector>

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

// ********** the transaction number, read without a decode *******************

/*
 * grdw_confirmed_transaction_peek_id() reads field 1 of a ConfirmedTransaction and steps over
 * everything else. What it answers has to be what the full decode answers -- that is the one
 * property that makes it safe to key a store by -- and it has to refuse what the full decode
 * would not read, rather than report a number out of bytes that are no message.
 */

namespace {

constexpr auto kConfirmedCommunityRoot =
    "CHkS3gEKZgpkCiCBZwMplGmI7fRR9MQkaR2Dz1qQQ5BCiC1btyJD71Ue9BJAtT7yJ8kBub5BCxCDG5wZ8s/"
    "dFKf2ystCXQQc4lZnkZmffBwTO6Udq5LupPfaAbvyFIt7942U+"
    "kHiuF52wokQChJ0WmYKIIFnAymUaYjt9FH0xCRpHYPPWpBDkEKILVu3IkPvVR70EiDX46igkKpEhzJG9cas/Bf/"
    "dO4XT1bnvSpV/7gQQfbbHRogrYcHSiqkvALTeX+7Q8iyvm7dbLHWNEqUD8UovOHhMLISBgiAzLn/BRiIgAwaBgjC8rn/"
    "BSCIgAwqIGHF/azvYntEu9pwC3bmSL61/"
    "Ob0pLcCTWspFwaJb4Q7MhQaEAoKCIDMuf8FELeVERICGHkIAjo3CiDbDtYSWhTwMKvtG/"
    "yDHgohjPn6v87n7NWBwMDniPAXxxCQThoQAZ4sMaMDdcCUHvNcWeT5eA==";

std::vector<uint8_t> FixtureBytes() {
  const size_t characters = std::strlen(kConfirmedCommunityRoot);
  std::vector<uint8_t> raw(ARNM_BASE64_BINARY_SIZE(characters) + 8u, 0u);
  uint32_t size = 0;
  EXPECT_EQ(ARNM_SUCCESS, arnm_binary_from_base64(raw.data(), &size, kConfirmedCommunityRoot));
  raw.resize(size);
  return raw;
}

arnm_result Peek(const std::vector<uint8_t> &bytes, uint64_t *out) {
  const arnm_memory_block block = {
      const_cast<uint8_t *>(bytes.data()), static_cast<uint32_t>(bytes.size())
  };
  return grdw_confirmed_transaction_peek_id(&block, out);
}

/** What the reader says for bytes that are no well formed top level, with @p out untouched. */
void ExpectRefused(const std::vector<uint8_t> &bytes, const char *what) {
  uint64_t out = 0xDEADBEEFu;
  EXPECT_EQ(ARNM_ERROR_DECODE_FAILED, Peek(bytes, &out)) << what;
  EXPECT_EQ(0xDEADBEEFu, out) << what << ": a refusal leaves the output alone";
}

} // namespace

TEST(ConfirmedTransactionPeekId, AgreesWithTheFullDecodeOnARealTransaction) {
  const std::vector<uint8_t> raw = FixtureBytes();
  uint64_t peeked = 0;
  ASSERT_EQ(ARNM_SUCCESS, Peek(raw, &peeked));

  arnm arena{};
  ASSERT_EQ(ARNM_SUCCESS, arnm_init_arena(&arena, 256u * 1024u));
  grdw_confirmed_transaction wire;
  grdw_confirmed_transaction_init(&wire);
  const arnm_memory_block block = {
      const_cast<uint8_t *>(raw.data()), static_cast<uint32_t>(raw.size())
  };
  ASSERT_EQ(ARNM_SUCCESS, grdw_confirmed_transaction_decode(&wire, &block, &arena));
  EXPECT_EQ(wire.id, peeked);
  EXPECT_EQ(121u, peeked);
  arnm_release(&arena);
}

TEST(ConfirmedTransactionPeekId, AgreesWithTheFullDecodeForEveryWidthOfNumber) {
  // one byte, the edge to two, the edge to three, past 32 bits, the top bit, all of them
  const std::vector<uint8_t> raw = FixtureBytes();
  arnm arena{};
  ASSERT_EQ(ARNM_SUCCESS, arnm_init_arena(&arena, 1024u * 1024u));
  const arnm_memory_block source = {
      const_cast<uint8_t *>(raw.data()), static_cast<uint32_t>(raw.size())
  };
  grdw_confirmed_transaction wire;
  grdw_confirmed_transaction_init(&wire);
  ASSERT_EQ(ARNM_SUCCESS, grdw_confirmed_transaction_decode(&wire, &source, &arena));

  for (const uint64_t id :
       {uint64_t{1}, uint64_t{127}, uint64_t{128}, uint64_t{16383}, uint64_t{16384},
        uint64_t{4294967296u}, uint64_t{1} << 63, UINT64_MAX}) {
    wire.id = id;
    std::vector<uint8_t> room(raw.size() + 64u, 0u);
    arnm_memory_block destination = {room.data(), static_cast<uint32_t>(room.size())};
    int written = 0;
    ASSERT_EQ(
        ARNM_SUCCESS, grdw_confirmed_transaction_encode(&destination, &written, &wire, &arena)
    );
    room.resize(static_cast<size_t>(written));

    uint64_t peeked = 0;
    ASSERT_EQ(ARNM_SUCCESS, Peek(room, &peeked)) << id;
    EXPECT_EQ(id, peeked);
  }
  arnm_release(&arena);
}

TEST(ConfirmedTransactionPeekId, FindsTheNumberWhereverItStands) {
  // every wire type the message can hold, in front of field 1: nothing is assumed about order
  const std::vector<uint8_t> bytes = {
      0x1A, 0x02, 0x08, 0x05,                  // field 3, a length delimited message
      0x20, 0x96, 0x01,                        // field 4, a varint of two bytes
      0x49, 1,    2,    3,    4,   5, 6, 7, 8, // field 9, fixed64
      0x55, 1,    2,    3,    4,               // field 10, fixed32
      0x08, 0x2A,                              // field 1 = 42
      0x3A, 0x03, 0xAA, 0xBB, 0xCC             // field 7, after it
  };
  uint64_t out = 0;
  ASSERT_EQ(ARNM_SUCCESS, Peek(bytes, &out));
  EXPECT_EQ(42u, out);
}

TEST(ConfirmedTransactionPeekId, TheLastOfTwoCounts) {
  const std::vector<uint8_t> bytes = {0x08, 0x05, 0x1A, 0x00, 0x08, 0x07};
  uint64_t out = 0;
  ASSERT_EQ(ARNM_SUCCESS, Peek(bytes, &out));
  EXPECT_EQ(7u, out) << "a singular field given twice takes the later value";
}

TEST(ConfirmedTransactionPeekId, AMessageWithoutANumberSaysZero) {
  uint64_t out = 99;
  ASSERT_EQ(ARNM_SUCCESS, Peek({0x1A, 0x00}, &out));
  EXPECT_EQ(0u, out) << "proto3 leaves a 0 out; 0 is no transaction number, the caller refuses";
  out = 99;
  ASSERT_EQ(ARNM_SUCCESS, Peek({}, &out));
  EXPECT_EQ(0u, out) << "an empty message is every field at its default";
}

TEST(ConfirmedTransactionPeekId, RefusesATopLevelThatIsNoMessage) {
  ExpectRefused({0x08, 0x80}, "a varint cut off");
  ExpectRefused({0x08}, "a tag with nothing after it");
  ExpectRefused({0x12, 0x05, 0x00}, "a length reaching past the end");
  ExpectRefused({0x49, 1, 2, 3}, "a fixed64 cut short");
  ExpectRefused({0x55, 1, 2}, "a fixed32 cut short");
  ExpectRefused({0x00, 0x00}, "field number 0");
  ExpectRefused({0x13}, "a group, which this message never holds");
  ExpectRefused({0x16}, "a reserved wire type");
  ExpectRefused({0x0A, 0x01, 0x00}, "field 1 as a length delimited field");
  ExpectRefused({0x09, 1, 2, 3, 4, 5, 6, 7, 8}, "field 1 as a fixed64");
  // eleven bytes of varint, and ten whose last carries more than the one bit left
  ExpectRefused(
      {0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01},
      "a varint longer than ten bytes"
  );
  ExpectRefused(
      {0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02}, "a varint past 64 bits"
  );
}

TEST(ConfirmedTransactionPeekId, NullIsRefusedNotRead) {
  uint64_t out = 0;
  const arnm_memory_block pointing_nowhere = {nullptr, 4};
  EXPECT_EQ(ARNM_ERROR_NULL_POINTER, grdw_confirmed_transaction_peek_id(nullptr, &out));
  EXPECT_EQ(ARNM_ERROR_NULL_POINTER, grdw_confirmed_transaction_peek_id(&pointing_nowhere, &out));
  const std::vector<uint8_t> bytes = {0x08, 0x01};
  const arnm_memory_block block = {const_cast<uint8_t *>(bytes.data()), 2};
  EXPECT_EQ(ARNM_ERROR_NULL_POINTER, grdw_confirmed_transaction_peek_id(&block, nullptr));
}
