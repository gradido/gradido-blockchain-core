#include "../terminal_colors.h"
#include "gradido_blockchain_core/data/timestamp.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"
#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/data/wire/hiero.h"
#include "gradido_blockchain_core/data/wire/ledger_anchor.h"
#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/utils/version.h"
#include "hostmem/memory.h"
#include "hostmem/mono_timer.h"
#include "hostmem/multi_arena.h"
#include "key_pairs.h"

#include "memory_limit.h"
#include "sodium.h"
#include <assert.h>
#include <gtest/gtest.h>
#include <string.h>

// Transaction Body
// contain only createAt and version string
constexpr auto emptyTransactionBodyBase64 = "CgASCAiAzLn/BRAAGgMzLjMgAA==";
constexpr auto transactionBodyOtherCommunityBase64 = "EgYIgMy5/wUYiIAMKhABniwxowN1wJQe81xZ5Pl4";
constexpr auto communityRootTransactionBodyBase64 =
    "WmYKIIFnAymUaYjt9FH0xCRpHYPPWpBDkEKILVu3IkPvVR70EiDX46igkKpEhzJG9cas/Bf/dO4XT1bnvSpV/"
    "7gQQfbbHRogrYcHSiqkvALTeX+7Q8iyvm7dbLHWNEqUD8UovOHhMLISBgiAzLn/BRiIgAw=";
constexpr auto registerAddressTransactionBodyBase64 =
    "SmoKIPTdOYn3VUt6sy490Lf54Rr86QoYEenR9ncWnrRL9EJyEAEaIGPNFY6lZapL/"
    "RYfuH7yyZBvPXAPm+BzMmX+h3Mv9TcQIiDbDtYSWhTwMKvtG/yDHgohjPn6v87n7NWBwMDniPAXxygBEgYIgMy5/"
    "wUYiIAM";
constexpr auto creationTransactionBodyBase64 = "OkMKOQog2w7WEloU8DCr7Rv8gx4KIYz5+r/"
                                               "O5+"
                                               "zVgcDA54jwF8cQgK3iBBoQAZ4sMaMDdcCUHvNcWeT5eBoGCLjKu"
                                               "f8FChEIAhINSGVsbG8gV29ybGQyABIGCIDMuf8FGIiADA==";
constexpr auto transferTransactionBodyBase64 =
    "MlwKOAog2w7WEloU8DCr7Rv8gx4KIYz5+r/O5+zVgcDA54jwF8cQoI0GGhABniwxowN1wJQe81xZ5Pl4EiAkTSjXzFvo/"
    "o+w2OHRuQ3nYDOGCC15POiHT2NX5uUyrQoRCAISDUhlbGxvIFdvcmxkMgASCgiAzLn/BRC3lREYiIAM";
constexpr auto deferredTransferTransactionBodyBase64 =
    "UmQKXAo4CiDbDtYSWhTwMKvtG/"
    "yDHgohjPn6v87n7NWBwMDniPAXxxCgjQYaEAGeLDGjA3XAlB7zXFnk+XgSICRNKNfMW+j+"
    "j7DY4dG5DedgM4YILXk86IdPY1fm5TKtEgQI7YYBChEIAhINSGVsbG8gV29ybGQyABIKCIDMuf8FELeVERiIgAw=";
constexpr auto redeemDeferredTransferTransactionBodyBase64 =
    "YmAIDxJcCjgKINsO1hJaFPAwq+0b/IMeCiGM+fq/"
    "zufs1YHAwOeI8BfHEKCNBhoQAZ4sMaMDdcCUHvNcWeT5eBIgJE0o18xb6P6PsNjh0bkN52AzhggteTzoh09jV+"
    "blMq0KEQgCEg1IZWxsbyBXb3JsZDIAEgoIgMy5/wUQt5URGIiADA==";
constexpr auto timeoutDeferredTransferTransactionBodyBase64 = "agIIEBIKCIDMuf8FELeVERiIgAw=";

// Gradido Transaction
constexpr auto communityRootTransactionBase64 =
    "CmYKZAoggWcDKZRpiO30UfTEJGkdg89akEOQQogtW7ciQ+9VHvQSQLU+8ifJAbm+"
    "QQsQgxucGfLP3RSn9srLQl0EHOJWZ5GZn3wcEzulHauS7qT32gG78hSLe/"
    "eNlPpB4rhedsKJEAoSdFpmCiCBZwMplGmI7fRR9MQkaR2Dz1qQQ5BCiC1btyJD71Ue9BIg1+OooJCqRIcyRvXGrPwX/"
    "3TuF09W570qVf+4EEH22x0aIK2HB0oqpLwC03l/u0PIsr5u3Wyx1jRKlA/FKLzh4TCyEgYIgMy5/wUYiIAM";
constexpr auto timeoutDeferredTransferTransactionBase64 =
    "EhRqAggQEgoIgMy5/wUQt5URGIiADBoEIBEIAw==";

// Confirmed Gradido Transaction
constexpr auto confirmedCommunityRootTransactionBase64 =
    "CHkS3gEKZgpkCiCBZwMplGmI7fRR9MQkaR2Dz1qQQ5BCiC1btyJD71Ue9BJAtT7yJ8kBub5BCxCDG5wZ8s/"
    "dFKf2ystCXQQc4lZnkZmffBwTO6Udq5LupPfaAbvyFIt7942U+"
    "kHiuF52wokQChJ0WmYKIIFnAymUaYjt9FH0xCRpHYPPWpBDkEKILVu3IkPvVR70EiDX46igkKpEhzJG9cas/Bf/"
    "dO4XT1bnvSpV/7gQQfbbHRogrYcHSiqkvALTeX+7Q8iyvm7dbLHWNEqUD8UovOHhMLISBgiAzLn/BRiIgAwaBgjC8rn/"
    "BSCIgAwqIGHF/azvYntEu9pwC3bmSL61/"
    "Ob0pLcCTWspFwaJb4Q7MhQaEAoKCIDMuf8FELeVERICGHkIAjo3CiDbDtYSWhTwMKvtG/"
    "yDHgohjPn6v87n7NWBwMDniPAXxxCQThoQAZ4sMaMDdcCUHvNcWeT5eEAC";

constexpr auto communityUuidHex = "019e2c31a30375c0941ef35c59e4f978";

grdd_timestamp createdAt1 = {.seconds = 1609459200, .nanos = 0};
grdd_timestamp createdAt2 = {.seconds = 1609459200, .nanos = 281271};
grdd_timestamp confirmedAt = {.seconds = 1609464130, .nanos = 0};
grdw_timestamp_seconds targetDate = {.seconds = 1609459000};
constexpr size_t BUFFER_SIZE = 512;

/*
 * One stretch for pbtools, shared by every test below. The workspace is written and then
 * finished with -- the wire structures copy out what they keep before the call returns -- so the
 * same bytes serve one test after another. Generously sized: what this file exercises is the
 * decoding, not the sizing, and the tests that do exercise the sizing bring their own.
 */
alignas(8) static uint8_t g_pbWorkspaceBytes[BUFFER_SIZE * 16];
static hostmem_memory_block g_pbWorkspace = {g_pbWorkspaceBytes, sizeof(g_pbWorkspaceBytes)};

static hostmem_memory_block fromBase64(
    const char *base64String, size_t size, int variant = sodium_base64_VARIANT_ORIGINAL
) {
  hostmem_memory_block result{};
  size_t binSize = (size / 4) * 3;

  uint8_t *buffer = (uint8_t *)malloc(binSize);
  if (!buffer) { return result; }
  size_t resultBinSize = 0;
  const char *firstInvalidByte = nullptr;
  auto convertResult = sodium_base642bin(
      buffer, binSize, base64String, size, nullptr, &resultBinSize, &firstInvalidByte, variant
  );
  if (0 != convertResult) {
    printf("invalid base64: error at: %td\n", firstInvalidByte - base64String);
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

static std::string toBase64(
    hostmem_memory_block *data, int variant = sodium_base64_VARIANT_ORIGINAL
) {
  if (!data || !data->size) { return ""; }
  size_t encodedSize = sodium_base64_encoded_len(data->size, variant);
  uint8_t *buffer = (uint8_t *)malloc(encodedSize);
  if (!buffer) { return ""; }
  if (nullptr == sodium_bin2base64((char *)buffer, encodedSize, data->data, data->size, variant)) {
    free(buffer);
    return "";
  }
  std::string base64String((const char *)buffer, encodedSize - 1);
  free(buffer);
  return base64String;
}

static std::string toHex(hostmem_memory_block *data) {
  if (!data || !data->size) { return ""; }
  size_t hexSize = data->size * 2 + 1;
  uint8_t *buffer = (uint8_t *)malloc(hexSize);
  if (!buffer) { return ""; }
  sodium_bin2hex((char *)buffer, hexSize, data->data, data->size);
  std::string hex((char *)buffer, hexSize - 1);
  free(buffer);
  return hex;
}

static std::string toHex(uint8_t publicKey[32]) {
  uint8_t buffer[65];
  sodium_bin2hex((char *)buffer, 65, publicKey, 32);
  return std::string((char *)buffer, 64);
}

static hostmem_memory_block fromHex(const char *hex) {
  hostmem_memory_block result{};
  if (!hex) { return result; }
  size_t hex_size = strlen(hex);
  size_t binSize = hex_size / 2;
  if (binSize * 2 != hex_size) {
    printf("invalid hex size\n");
    return result;
  }
  result.size = binSize;
  result.data = (uint8_t *)malloc(binSize);
  size_t resultBinSize = 0;
  if (0 != sodium_hex2bin(result.data, binSize, hex, hex_size, nullptr, &resultBinSize, nullptr)) {
    printf("invalid hex: %s\n", hex);
  }
  return result;
}

TEST(PBToolsTest, TransactionBody_Decode) {
  hostmem_multi_arena mem{};
  grdw_transaction_body body{};
  auto bin = fromBase64(emptyTransactionBodyBase64, strlen(emptyTransactionBodyBase64));
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_transaction_body_decode(&body, &bin, &g_pbWorkspace, &mem);
  free(bin.data);
  hostmem_multi_arena_release(&mem);
}

/*
 * The workspace is the caller's to size, and a stretch that turns out too small says exactly
 * that and nothing else -- which is what lets a caller enlarge it and call again rather than
 * having to tell a bad message from a small buffer. This walks that loop once: too small,
 * OUT_OF_MEMORY, larger, decoded.
 */
TEST(PBToolsTest, TooSmallWorkspaceSaysSoAndALargerOneWorks) {
  init_key_pairs();
  // encode a real body first, so there is something that genuinely has to decode
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.created_at = createdAt1;
  body.transaction_type = GRDT_TRANSACTION_TRANSFER;
  memcpy(body.transfer.sender.pubkey, g_KeyPairs[0].public_key, SIGN_PUBLIC_KEY_SIZE);
  body.transfer.sender.amount = 12345;
  memcpy(body.transfer.recipient, g_KeyPairs[1].public_key, SIGN_PUBLIC_KEY_SIZE);

  uint8_t buffer[256]{};
  hostmem_memory_block encoded = {.data = buffer, .size = sizeof(buffer)};
  int finalSize = 0;
  ASSERT_EQ(
      grdw_transaction_body_encode(&encoded, &finalSize, &body, &g_pbWorkspace), HOSTMEM_SUCCESS
  );
  encoded.size = (uint32_t)finalSize;

  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);

  // 64 bytes: enough for pbtools to start in and not enough to finish, which is the case worth
  // separating from a message that is simply wrong
  alignas(8) uint8_t tinyBytes[64];
  hostmem_memory_block tiny = {tinyBytes, sizeof(tinyBytes)};
  grdw_transaction_body decoded;
  grdw_transaction_body_init(&decoded);
  EXPECT_EQ(
      grdw_transaction_body_decode(&decoded, &encoded, &tiny, &mem), HOSTMEM_ERROR_OUT_OF_MEMORY
  ) << "a stretch too small has to be told apart from a message that will never decode";

  // the same bytes, the same everything, a larger stretch -- which is all a caller has to change
  alignas(8) uint8_t roomyBytes[4096];
  hostmem_memory_block roomy = {roomyBytes, sizeof(roomyBytes)};
  grdw_transaction_body_init(&decoded);
  ASSERT_EQ(grdw_transaction_body_decode(&decoded, &encoded, &roomy, &mem), HOSTMEM_SUCCESS);
  EXPECT_EQ(decoded.transaction_type, GRDT_TRANSACTION_TRANSFER);
  EXPECT_EQ(decoded.transfer.sender.amount, 12345);

  hostmem_multi_arena_release(&mem);
}

// A failing decode leaves the workspace as it found it: the stretch is the caller's, and what
// pbtools wrote before it gave up is exactly what someone reads when a message will not decode.
// This test exists so the next reviewer -- human or not -- sees that the retained bytes are a
// decision and not an oversight. See include/gradido_blockchain_core/data/wire/pb_workspace.h.
TEST(PBToolsTest, FailedDecodeLeavesTheCallersWorkspaceAlone) {
  uint8_t garbage[64];
  for (size_t i = 0; i < sizeof(garbage); ++i) {
    garbage[i] = static_cast<uint8_t>(0xF0 | (i & 7));
  }
  hostmem_memory_block bin{garbage, sizeof(garbage)};

  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);

  // zeroed first, so anything non-zero afterwards is what pbtools left behind
  alignas(8) uint8_t workspaceBytes[BUFFER_SIZE]{};
  hostmem_memory_block workspace = {workspaceBytes, sizeof(workspaceBytes)};

  grdw_transaction_body body{};
  EXPECT_EQ(
      grdw_transaction_body_decode(&body, &bin, &workspace, &mem), HOSTMEM_ERROR_DECODE_FAILED
  );

  // the descriptor is untouched -- this is the caller's block and nothing here shrinks or frees
  // it -- and the bytes still carry the half-read message
  EXPECT_EQ(workspace.data, workspaceBytes);
  EXPECT_EQ(workspace.size, sizeof(workspaceBytes));
  bool anythingWritten = false;
  for (size_t i = 0; i < sizeof(workspaceBytes); ++i) {
    if (workspaceBytes[i]) {
      anythingWritten = true;
      break;
    }
  }
  EXPECT_TRUE(anythingWritten) << "the workspace is left for inspection, do not clear it";

  hostmem_multi_arena_release(&mem);
}

// reserve_memos took the address of a member before it knew the body existed, so a NULL body
// was a crash rather than an error. The init functions next to it had the same gap, while
// grdc_sign_key_pair_init and grdr_complete_transaction_init have always guarded.
TEST(PBToolsTest, WireEntryPointsSurviveANullPointer) {
  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);

  EXPECT_EQ(grdw_transaction_body_reserve_memos(nullptr, 4, &mem), HOSTMEM_ERROR_NULL_POINTER);
  // the null check comes first: an out of range count must not decide the outcome for a body
  // that was never there
  EXPECT_EQ(grdw_transaction_body_reserve_memos(nullptr, 999, &mem), HOSTMEM_ERROR_NULL_POINTER);

  // and a valid body still reserves as before
  grdw_transaction_body body{};
  grdw_transaction_body_init(&body);
  EXPECT_EQ(grdw_transaction_body_reserve_memos(&body, 4, &mem), HOSTMEM_SUCCESS);
  EXPECT_EQ(body.memos_count, 4u);
  EXPECT_EQ(grdw_transaction_body_reserve_memos(&body, 256, &mem), HOSTMEM_ERROR_INVALID_PARAM);
  grdw_transaction_body_free(&body, &mem);

  // the init functions simply do nothing rather than writing through a null pointer
  grdw_transaction_body_init(nullptr);
  grdw_gradido_transaction_init(nullptr);
  grdw_confirmed_transaction_init(nullptr);

  hostmem_multi_arena_release(&mem);
}

// A decode that fails says so with a decode error. All three wire decoders used to report
// HOSTMEM_ERROR_ENCODE_FAILED for a malformed buffer, which sends a reader looking at the
// wrong half of the code. Nothing covered the failing path, so nothing objected.
TEST(PBToolsTest, DecodeOfGarbageReportsDecodeFailed) {
  uint8_t garbage[64];
  for (size_t i = 0; i < sizeof(garbage); ++i) {
    garbage[i] = static_cast<uint8_t>(0xF0 | (i & 7));
  }
  hostmem_memory_block bin{garbage, sizeof(garbage)};

  {
    hostmem_multi_arena mem{};
    grdw_transaction_body body{};
    ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
    EXPECT_EQ(
        grdw_transaction_body_decode(&body, &bin, &g_pbWorkspace, &mem), HOSTMEM_ERROR_DECODE_FAILED
    );
    hostmem_multi_arena_release(&mem);
  }
  {
    hostmem_multi_arena mem{};
    grdw_gradido_transaction tx{};
    ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
    EXPECT_EQ(
        grdw_gradido_transaction_decode(&tx, &bin, &g_pbWorkspace, &mem),
        HOSTMEM_ERROR_DECODE_FAILED
    );
    hostmem_multi_arena_release(&mem);
  }
  {
    hostmem_multi_arena mem{};
    grdw_confirmed_transaction confirmed{};
    ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
    EXPECT_EQ(
        grdw_confirmed_transaction_decode(&confirmed, &bin, &g_pbWorkspace, &mem),
        HOSTMEM_ERROR_DECODE_FAILED
    );
    hostmem_multi_arena_release(&mem);
  }
}

TEST(PBtoolsTest, TransactionBody_Encode_OtherCommunity) {
  init_key_pairs();
  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.created_at = createdAt1;

  hostmem_memory_block communityUuid = fromHex(communityUuidHex);
  hostmem_multi_arena_alloc(&body.other_community_uuid, 16, &mem);
  memcpy(body.other_community_uuid, communityUuid.data, 16);
  free(communityUuid.data);

  body.transaction_type = GRDT_TRANSACTION_NONE;
  uint8_t buffer[256]{};
  hostmem_memory_block bufferPtr = {.data = buffer, .size = 256};
  int finalSize = 0;
  ASSERT_EQ(
      grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &g_pbWorkspace), HOSTMEM_SUCCESS
  );
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  EXPECT_STREQ(base64.data(), transactionBodyOtherCommunityBase64);
  // printf("other community:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());

  hostmem_multi_arena_release(&mem);
}

TEST(PBtoolsTest, TransactionBody_Decode_OtherCommunity) {
  hostmem_multi_arena mem{};
  grdw_transaction_body body{};
  auto bin =
      fromBase64(transactionBodyOtherCommunityBase64, strlen(transactionBodyOtherCommunityBase64));
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &g_pbWorkspace, &mem), HOSTMEM_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.type, GRDT_CROSS_GROUP_LOCAL);
  EXPECT_FALSE(body.memos);
  EXPECT_FALSE(body.memos_count);
  EXPECT_EQ(body.transaction_type, GRDT_TRANSACTION_NONE);
  hostmem_memory_block communityUuid = fromHex(communityUuidHex);
  ASSERT_TRUE(body.other_community_uuid);
  EXPECT_FALSE(memcmp(body.other_community_uuid, communityUuid.data, 16));
  free(communityUuid.data);

  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_CommunityRoot_Encode) {
  init_key_pairs();
  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.created_at = createdAt1;

  grdw_community_root_assemble(
      &body.community_root, g_KeyPairs[0].public_key, g_KeyPairs[1].public_key,
      g_KeyPairs[2].public_key
  );

  body.transaction_type = GRDT_TRANSACTION_COMMUNITY_ROOT;
  uint8_t buffer[256]{};
  hostmem_memory_block bufferPtr = {.data = buffer, .size = 256};
  int finalSize = 0;
  ASSERT_EQ(
      grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &g_pbWorkspace), HOSTMEM_SUCCESS
  );
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  EXPECT_STREQ(base64.data(), communityRootTransactionBodyBase64);
  // printf("community root:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());

  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_CommunityRoot_Decode) {
  hostmem_multi_arena mem{};
  grdw_transaction_body body{};
  auto bin =
      fromBase64(communityRootTransactionBodyBase64, strlen(communityRootTransactionBodyBase64));
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &g_pbWorkspace, &mem), HOSTMEM_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt1.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt1.nanos);
  EXPECT_EQ(body.type, GRDT_CROSS_GROUP_LOCAL);
  EXPECT_FALSE(body.memos);
  EXPECT_FALSE(body.memos_count);
  EXPECT_EQ(body.transaction_type, GRDT_TRANSACTION_COMMUNITY_ROOT);
  EXPECT_FALSE(memcmp(body.community_root.pubkey, g_KeyPairs[0].public_key, 32));
  EXPECT_FALSE(memcmp(body.community_root.gmw_pubkey, g_KeyPairs[1].public_key, 32));
  EXPECT_FALSE(memcmp(body.community_root.auf_pubkey, g_KeyPairs[2].public_key, 32));
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_RegisterAddress_Encode) {
  init_key_pairs();
  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.created_at = createdAt1;

  uint8_t nameHash[32];
  crypto_generichash(nameHash, 32, g_KeyPairs[3].public_key, 32, NULL, 0);
  grdw_register_address_assemble(
      &body.register_address, g_KeyPairs[3].public_key, GRDT_ADDRESS_COMMUNITY_HUMAN, 1, nameHash,
      g_KeyPairs[4].public_key
  );

  body.transaction_type = GRDT_TRANSACTION_REGISTER_ADDRESS;
  uint8_t buffer[256]{};
  hostmem_memory_block bufferPtr = {.data = buffer, .size = 256};
  int finalSize = 0;
  ASSERT_EQ(
      grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &g_pbWorkspace), HOSTMEM_SUCCESS
  );
  // printf("finalSize: %lld\n", finalSize);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  EXPECT_EQ(base64, registerAddressTransactionBodyBase64);
  // printf("register address:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());

  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_RegisterAddress_Decode) {
  hostmem_multi_arena mem{};
  grdw_transaction_body body{};
  auto bin = fromBase64(
      registerAddressTransactionBodyBase64, strlen(registerAddressTransactionBodyBase64)
  );
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &g_pbWorkspace, &mem), HOSTMEM_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt1.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt1.nanos);
  EXPECT_EQ(body.type, GRDT_CROSS_GROUP_LOCAL);
  EXPECT_FALSE(body.memos);
  EXPECT_FALSE(body.memos_count);
  EXPECT_EQ(body.transaction_type, GRDT_TRANSACTION_REGISTER_ADDRESS);
  EXPECT_FALSE(memcmp(body.register_address.user_pubkey, g_KeyPairs[3].public_key, 32));
  EXPECT_FALSE(memcmp(body.register_address.account_pubkey, g_KeyPairs[4].public_key, 32));
  EXPECT_EQ(body.register_address.address_type, GRDT_ADDRESS_COMMUNITY_HUMAN);
  EXPECT_EQ(body.register_address.derivation_index, 1);
  EXPECT_EQ(crypto_generichash_BYTES, 32);
  uint8_t nameHash[crypto_generichash_BYTES];
  crypto_generichash(nameHash, crypto_generichash_BYTES, g_KeyPairs[3].public_key, 32, NULL, 0);
  EXPECT_FALSE(memcmp(body.register_address.name_hash, nameHash, 32));
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_Creation_Encode) {
  init_key_pairs();
  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  grdw_transaction_body_reserve_memos(&body, 1, &mem);
  grdw_encrypted_memo memo = {.type = GRDT_MEMO_KEY_PLAIN};
  hostmem_multi_arena_alloc(&memo.memo.data, 13, &mem);
  memo.memo.size = 13;
  assert(memo.memo.data);
  memcpy(memo.memo.data, "Hello World2", 13);
  grdw_transaction_body_move_memo(&body, &memo, 0);
  body.created_at = createdAt1;

  hostmem_memory_block communityUuid = fromHex(communityUuidHex);
  grdw_gradido_creation_assemble(
      &body.creation, g_KeyPairs[4].public_key, 10000000, communityUuid.data, targetDate.seconds
  );
  free(communityUuid.data);

  body.transaction_type = GRDT_TRANSACTION_CREATION;
  uint8_t buffer[256]{};
  hostmem_memory_block bufferPtr = {.data = buffer, .size = 256};
  int finalSize = 0;
  ASSERT_EQ(
      grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &g_pbWorkspace), HOSTMEM_SUCCESS
  );
  // printf("finalSize: %lld\n", finalSize);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  // printf("creation:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  EXPECT_EQ(base64, creationTransactionBodyBase64);
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_Creation_Decode) {
  hostmem_multi_arena mem{};
  grdw_transaction_body body{};
  auto bin = fromBase64(creationTransactionBodyBase64, strlen(creationTransactionBodyBase64));
  EXPECT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &g_pbWorkspace, &mem), HOSTMEM_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt1.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt1.nanos);
  EXPECT_EQ(body.type, GRDT_CROSS_GROUP_LOCAL);
  EXPECT_TRUE(body.memos);
  EXPECT_EQ(body.memos_count, 1);
  EXPECT_EQ(body.memos[0].type, GRDT_MEMO_KEY_PLAIN);
  EXPECT_STREQ((const char *)body.memos[0].memo.data, "Hello World2");
  EXPECT_EQ(body.transaction_type, GRDT_TRANSACTION_CREATION);
  EXPECT_FALSE(memcmp(body.creation.recipient.pubkey, g_KeyPairs[4].public_key, 32));
  EXPECT_EQ(body.creation.recipient.amount, 10000000);
  hostmem_memory_block communityUuid = fromHex(communityUuidHex);
  EXPECT_FALSE(memcmp(body.creation.recipient.community_uuid, communityUuid.data, 16));
  free(communityUuid.data);
  EXPECT_EQ(body.creation.target_date.seconds, targetDate.seconds);
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_Transfer_Encode) {
  init_key_pairs();
  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  grdw_transaction_body_reserve_memos(&body, 1, &mem);
  grdw_encrypted_memo memo = {.type = GRDT_MEMO_KEY_PLAIN};
  hostmem_multi_arena_alloc(&memo.memo.data, 13, &mem);
  memo.memo.size = 13;
  assert(memo.memo.data);
  memcpy(memo.memo.data, "Hello World2", 13);
  grdw_transaction_body_move_memo(&body, &memo, 0);
  body.created_at = createdAt2;

  hostmem_memory_block communityUuid = fromHex(communityUuidHex);
  grdw_gradido_transfer_assemble(
      &body.transfer, g_KeyPairs[4].public_key, 100000, communityUuid.data, g_KeyPairs[5].public_key
  );
  free(communityUuid.data);

  body.transaction_type = GRDT_TRANSACTION_TRANSFER;
  uint8_t buffer[256]{};
  hostmem_memory_block bufferPtr = {.data = buffer, .size = 256};
  int finalSize = 0;
  ASSERT_EQ(
      grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &g_pbWorkspace), HOSTMEM_SUCCESS
  );
  // printf("finalSize: %lld\n", finalSize);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  // printf("transfer:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  EXPECT_EQ(base64, transferTransactionBodyBase64);
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_Transfer_Decode) {
  hostmem_multi_arena mem{};
  grdw_transaction_body body{};
  auto bin = fromBase64(transferTransactionBodyBase64, strlen(transferTransactionBodyBase64));
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &g_pbWorkspace, &mem), HOSTMEM_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt2.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt2.nanos);
  EXPECT_EQ(body.type, GRDT_CROSS_GROUP_LOCAL);
  EXPECT_TRUE(body.memos);
  EXPECT_EQ(body.memos_count, 1);
  EXPECT_EQ(body.memos[0].type, GRDT_MEMO_KEY_PLAIN);
  EXPECT_STREQ((const char *)body.memos[0].memo.data, "Hello World2");
  EXPECT_EQ(body.transaction_type, GRDT_TRANSACTION_TRANSFER);
  EXPECT_FALSE(memcmp(body.transfer.sender.pubkey, g_KeyPairs[4].public_key, 32));
  EXPECT_EQ(body.transfer.sender.amount, 100000);
  hostmem_memory_block communityUuid = fromHex(communityUuidHex);
  EXPECT_FALSE(memcmp(body.transfer.sender.community_uuid, communityUuid.data, 16));
  free(communityUuid.data);
  EXPECT_FALSE(memcmp(body.transfer.recipient, g_KeyPairs[5].public_key, 32));
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_Deferred_Transfer_Encode) {
  init_key_pairs();
  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  grdw_transaction_body_reserve_memos(&body, 1, &mem);
  grdw_encrypted_memo memo = {.type = GRDT_MEMO_KEY_PLAIN};
  hostmem_multi_arena_alloc(&memo.memo.data, 13, &mem);
  memo.memo.size = 13;
  assert(memo.memo.data);
  memcpy(memo.memo.data, "Hello World2", 13);
  grdw_transaction_body_move_memo(&body, &memo, 0);
  body.created_at = createdAt2;

  hostmem_memory_block communityUuid = fromHex(communityUuidHex);
  grdw_gradido_deferred_transfer_assemble(
      &body.deferred_transfer, g_KeyPairs[4].public_key, 100000, communityUuid.data,
      g_KeyPairs[5].public_key, 17261
  );
  free(communityUuid.data);

  body.transaction_type = GRDT_TRANSACTION_DEFERRED_TRANSFER;
  uint8_t buffer[256]{};
  hostmem_memory_block bufferPtr = {.data = buffer, .size = 256};
  int finalSize = 0;
  ASSERT_EQ(
      grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &g_pbWorkspace), HOSTMEM_SUCCESS
  );
  // printf("finalSize: %lld\n", finalSize);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  // printf("deferred transfer:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  EXPECT_EQ(base64, deferredTransferTransactionBodyBase64);
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_Deferred_Transfer_Decode) {
  hostmem_multi_arena mem{};
  grdw_transaction_body body{};
  auto bin = fromBase64(
      deferredTransferTransactionBodyBase64, strlen(deferredTransferTransactionBodyBase64)
  );
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &g_pbWorkspace, &mem), HOSTMEM_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt2.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt2.nanos);
  EXPECT_EQ(body.type, GRDT_CROSS_GROUP_LOCAL);
  EXPECT_TRUE(body.memos);
  EXPECT_EQ(body.memos_count, 1);
  EXPECT_EQ(body.memos[0].type, GRDT_MEMO_KEY_PLAIN);
  EXPECT_STREQ((const char *)body.memos[0].memo.data, "Hello World2");
  EXPECT_EQ(body.transaction_type, GRDT_TRANSACTION_DEFERRED_TRANSFER);
  EXPECT_FALSE(memcmp(body.deferred_transfer.transfer.sender.pubkey, g_KeyPairs[4].public_key, 32));
  EXPECT_EQ(body.deferred_transfer.transfer.sender.amount, 100000);
  hostmem_memory_block communityUuid = fromHex(communityUuidHex);
  EXPECT_FALSE(
      memcmp(body.deferred_transfer.transfer.sender.community_uuid, communityUuid.data, 16)
  );
  free(communityUuid.data);
  EXPECT_FALSE(memcmp(body.deferred_transfer.transfer.recipient, g_KeyPairs[5].public_key, 32));
  EXPECT_EQ(body.deferred_transfer.timeout_duration, 17261);
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_Redeem_Deferred_Transfer_Encode) {
  init_key_pairs();
  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  grdw_transaction_body_reserve_memos(&body, 1, &mem);
  grdw_encrypted_memo memo = {.type = GRDT_MEMO_KEY_PLAIN};
  hostmem_multi_arena_alloc(&memo.memo.data, 13, &mem);
  memo.memo.size = 13;
  assert(memo.memo.data);
  memcpy(memo.memo.data, "Hello World2", 13);
  grdw_transaction_body_move_memo(&body, &memo, 0);
  body.created_at = createdAt2;

  hostmem_memory_block communityUuid = fromHex(communityUuidHex);
  grdw_gradido_redeem_deferred_transfer_assemble(
      &body.redeem_deferred_transfer, 15, g_KeyPairs[4].public_key, 100000, communityUuid.data,
      g_KeyPairs[5].public_key
  );
  free(communityUuid.data);

  body.transaction_type = GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER;
  uint8_t buffer[256]{};
  hostmem_memory_block bufferPtr = {.data = buffer, .size = 256};
  int finalSize = 0;
  ASSERT_EQ(
      grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &g_pbWorkspace), HOSTMEM_SUCCESS
  );
  // printf("finalSize: %lld\n", finalSize);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  // printf("redeem deferred transfer:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  EXPECT_EQ(base64, redeemDeferredTransferTransactionBodyBase64);
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_Redeem_Deferred_Transfer_Decode) {
  hostmem_multi_arena mem{};
  grdw_transaction_body body{};
  auto bin = fromBase64(
      redeemDeferredTransferTransactionBodyBase64,
      strlen(redeemDeferredTransferTransactionBodyBase64)
  );
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &g_pbWorkspace, &mem), HOSTMEM_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt2.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt2.nanos);
  EXPECT_EQ(body.type, GRDT_CROSS_GROUP_LOCAL);
  EXPECT_TRUE(body.memos);
  EXPECT_EQ(body.memos_count, 1);
  EXPECT_EQ(body.memos[0].type, GRDT_MEMO_KEY_PLAIN);
  EXPECT_STREQ((const char *)body.memos[0].memo.data, "Hello World2");
  EXPECT_EQ(body.transaction_type, GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER);
  EXPECT_FALSE(
      memcmp(body.redeem_deferred_transfer.transfer.sender.pubkey, g_KeyPairs[4].public_key, 32)
  );
  EXPECT_EQ(body.redeem_deferred_transfer.transfer.sender.amount, 100000);
  hostmem_memory_block communityUuid = fromHex(communityUuidHex);
  EXPECT_FALSE(
      memcmp(body.redeem_deferred_transfer.transfer.sender.community_uuid, communityUuid.data, 16)
  );
  free(communityUuid.data);
  EXPECT_FALSE(
      memcmp(body.redeem_deferred_transfer.transfer.recipient, g_KeyPairs[5].public_key, 32)
  );
  EXPECT_EQ(body.redeem_deferred_transfer.deferred_transfer_transaction_nr, 15);
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_Timeout_Deferred_Transfer_Encode) {
  init_key_pairs();
  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.created_at = createdAt2;

  grdw_gradido_timeout_deferred_transfer_assemble(&body.timeout_deferred_transfer, 16);

  body.transaction_type = GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER;
  uint8_t buffer[256]{};
  hostmem_memory_block bufferPtr = {.data = buffer, .size = 256};
  int finalSize = 0;
  ASSERT_EQ(
      grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &g_pbWorkspace), HOSTMEM_SUCCESS
  );
  // printf("finalSize: %lld\n", finalSize);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  // printf("timeout deferred transfer:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  EXPECT_EQ(base64, timeoutDeferredTransferTransactionBodyBase64);
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, TransactionBody_Timeout_Deferred_Transfer_Decode) {
  hostmem_multi_arena mem{};
  grdw_transaction_body body{};
  auto bin = fromBase64(
      timeoutDeferredTransferTransactionBodyBase64,
      strlen(timeoutDeferredTransferTransactionBodyBase64)
  );
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &g_pbWorkspace, &mem), HOSTMEM_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt2.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt2.nanos);
  EXPECT_EQ(body.type, GRDT_CROSS_GROUP_LOCAL);
  EXPECT_FALSE(body.memos);
  EXPECT_FALSE(body.memos_count);

  EXPECT_EQ(body.transaction_type, GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER);
  EXPECT_EQ(body.timeout_deferred_transfer.deferred_transfer_transaction_nr, 16);
  hostmem_multi_arena_release(&mem);
}

// ###############    Gradido Transaction    ############################################

TEST(PBToolsTest, GradidoTransaction_Encode_CommunityRoot) {
  hostmem_multi_arena mem{};
  grdw_gradido_transaction tx{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_gradido_transaction_init(&tx);
  tx.body_bytes =
      fromBase64(communityRootTransactionBodyBase64, strlen(communityRootTransactionBodyBase64));
  grdw_gradido_transaction_reserve_sig_map(&tx, 1, &mem);
  grdw_signature_pair signature{};
  memcpy(signature.public_key, g_KeyPairs[0].public_key, 32);
  unsigned long long sig_len = 64;
  crypto_sign_detached(
      signature.signature, &sig_len, (const unsigned char *)communityRootTransactionBodyBase64,
      strlen(communityRootTransactionBodyBase64), g_KeyPairs[0].private_key
  );
  ASSERT_EQ(grdw_gradido_transaction_copy_sig_map(&tx, &signature, 0), HOSTMEM_SUCCESS);
  uint8_t buffer[256]{};
  hostmem_memory_block bufferPtr = {.data = buffer, .size = 256};
  int finalSize = 0;
  ASSERT_EQ(
      grdw_gradido_transaction_encode(&bufferPtr, &finalSize, &tx, &g_pbWorkspace), HOSTMEM_SUCCESS
  );
  bufferPtr.size = finalSize;
  auto base64 = toBase64(&bufferPtr);
  ASSERT_EQ(base64, communityRootTransactionBase64);
  // printf("signed community root tx: %s\n", base64.c_str());
  auto hex = toHex(&bufferPtr);
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  free(tx.body_bytes.data);
  tx.body_bytes.data = nullptr;
  grdw_gradido_transaction_free(&tx, &mem);
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, GradidoTransaction_Decode_CommunityRoot_1000X) {
  hostmem_mono_timer timeUsed;
  hostmem_multi_arena mem{};
  grdw_gradido_transaction tx{};
  grdw_gradido_transaction_init(&tx);

  grdw_transaction_body body{};
  grdw_transaction_body_init(&body);

  hostmem_mono_timer_reset(&timeUsed);
  auto bin = fromBase64(communityRootTransactionBase64, strlen(communityRootTransactionBase64));
  alignas(8) uint8_t staticBuffer[BUFFER_SIZE * 2];
  // borrowed rather than owned, which is what keeps this loop free of the host: the chain
  // fills the caller's bytes and opens nothing of its own unless they run out
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE * 2, 0, nullptr), HOSTMEM_SUCCESS);
  ASSERT_EQ(hostmem_multi_arena_borrow(&mem, staticBuffer, BUFFER_SIZE * 2), HOSTMEM_SUCCESS);
  char buffer[256];
  hostmem_mono_timer_reset(&timeUsed);
  int i = 0;
  for (; i < 1000; ++i) {
    hostmem_multi_arena_reset(&mem);

    ASSERT_EQ(grdw_gradido_transaction_decode(&tx, &bin, &g_pbWorkspace, &mem), HOSTMEM_SUCCESS);
    ASSERT_EQ(
        grdw_transaction_body_decode(&body, &tx.body_bytes, &g_pbWorkspace, &mem), HOSTMEM_SUCCESS
    );

    grdw_transaction_body_free(&body, &mem);
    grdw_gradido_transaction_free(&tx, &mem);
  }
  free(bin.data);

  hostmem_mono_timer_string(buffer, 256, timeUsed);
  // printf("time for decode community root %d times: %s\n", i, buffer);
  std::cout << TIME_GTEST_BLUE << "time for decode community root " << i << " times: " << buffer
            << ANSI_TXT_DFT << std::endl;
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, GradidoTransaction_Encode_CommunityRoot_1000X) {
  hostmem_mono_timer timeUsed;
  hostmem_multi_arena mem{};

  grdw_gradido_transaction tx{};
  alignas(8) uint8_t staticBuffer[128]{};
  // the capacity is what an arena the chain opens *itself* gets; the borrowed 128 bytes below
  // are what it is meant to use, and a capacity of 128 would collide with the full threshold
  ASSERT_EQ(hostmem_multi_arena_init(&mem, 4096, 0, nullptr), HOSTMEM_SUCCESS);
  ASSERT_EQ(hostmem_multi_arena_borrow(&mem, staticBuffer, 128), HOSTMEM_SUCCESS);
  grdw_gradido_transaction_init(&tx);
  // tx.body_bytes = fromBase64(communityRootTransactionBodyBase64,
  // strlen(communityRootTransactionBodyBase64));
  grdw_gradido_transaction_reserve_sig_map(&tx, 1, &mem);
  grdw_signature_pair signature{};
  memcpy(signature.public_key, g_KeyPairs[0].public_key, 32);
  unsigned long long sig_len = 64;
  crypto_sign_detached(
      signature.signature, &sig_len, (const unsigned char *)communityRootTransactionBodyBase64,
      strlen(communityRootTransactionBodyBase64), g_KeyPairs[0].private_key
  );
  ASSERT_EQ(grdw_gradido_transaction_copy_sig_map(&tx, &signature, 0), HOSTMEM_SUCCESS);

  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.created_at = createdAt1;
  memcpy(body.community_root.pubkey, g_KeyPairs[0].public_key, 32);
  memcpy(body.community_root.gmw_pubkey, g_KeyPairs[1].public_key, 32);
  memcpy(body.community_root.auf_pubkey, g_KeyPairs[2].public_key, 32);
  body.transaction_type = GRDT_TRANSACTION_COMMUNITY_ROOT;

  // Borrowed storage, sized for the workspaces the two encodes now ask for: an encode scales
  // its workspace by the destination it is given, so 128 bytes of body destination and 256 of
  // transaction destination come to well under this. Borrowing rather than owning keeps the
  // thousand rounds below free of the host, which is what makes the timing worth printing.
  alignas(8) uint8_t staticBuffer2[8192]{}; // multiple of 8, as the arena requires
  uint8_t buffer[256]{};
  hostmem_memory_block bufferPtr = {.data = buffer, .size = 256};
  int finalSize = 0;
  hostmem_multi_arena mem2{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem2, sizeof(staticBuffer2), 0, nullptr), HOSTMEM_SUCCESS);
  ASSERT_EQ(
      hostmem_multi_arena_borrow(&mem2, staticBuffer2, sizeof(staticBuffer2)), HOSTMEM_SUCCESS
  );
  int i = 0;
  hostmem_mono_timer_reset(&timeUsed);
  for (; i < 1000; ++i) {
    hostmem_multi_arena_reset(&mem2);
    // the destination for the body, and then its own recorded length once it is written -- a
    // chain keeps the whole reservation until the reset either way, so nothing is given back
    // here that the reset would not give back anyway
    ASSERT_EQ(hostmem_multi_arena_alloc(&tx.body_bytes.data, 128, &mem2), HOSTMEM_SUCCESS);
    tx.body_bytes.size = 128;
    ASSERT_EQ(
        grdw_transaction_body_encode(&tx.body_bytes, &finalSize, &body, &g_pbWorkspace),
        HOSTMEM_SUCCESS
    );
    tx.body_bytes.size = (uint32_t)finalSize;
    ASSERT_EQ(
        grdw_gradido_transaction_encode(&bufferPtr, &finalSize, &tx, &g_pbWorkspace),
        HOSTMEM_SUCCESS
    );
  }
  tx.body_bytes.data = nullptr;
  tx.body_bytes.size = 0;
  char timerBuffer[256];
  hostmem_mono_timer_string(timerBuffer, 256, timeUsed);
  std::cout << TIME_GTEST_BLUE << "time for encode community root " << i
            << " times: " << timerBuffer << ANSI_TXT_DFT << std::endl;
  // printf("time for encode community root %d times: %s\n", i, timerBuffer);
  grdw_gradido_transaction_free(&tx, &mem);
  // the borrowed stretches are the caller's and are let go untouched; the chains' own
  // bookkeeping is not, and that is what these two release
  hostmem_multi_arena_release(&mem2);
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, GradidoTransaction_Encode_TimeoutDeferredTransfer) {
  hostmem_multi_arena mem{};
  grdw_gradido_transaction tx{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_gradido_transaction_init(&tx);
  tx.body_bytes = fromBase64(
      timeoutDeferredTransferTransactionBodyBase64,
      strlen(timeoutDeferredTransferTransactionBodyBase64)
  );
  tx.pairing_ledger_anchor.id = 17;
  tx.pairing_ledger_anchor.type = GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_ID;

  uint8_t buffer[256]{};
  hostmem_memory_block bufferPtr = {.data = buffer, .size = 256};
  int finalSize = 0;
  ASSERT_EQ(
      grdw_gradido_transaction_encode(&bufferPtr, &finalSize, &tx, &g_pbWorkspace), HOSTMEM_SUCCESS
  );
  bufferPtr.size = finalSize;
  auto base64 = toBase64(&bufferPtr);
  EXPECT_EQ(base64, timeoutDeferredTransferTransactionBase64);
  // printf("not signed gradido transacion (timeout deferred transfer): %s\n", base64.c_str());
  auto hex = toHex(&bufferPtr);
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  free(tx.body_bytes.data);
  tx.body_bytes.data = nullptr;
  grdw_gradido_transaction_free(&tx, &mem);
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, GradidoTransaction_Decode_TimeoutDeferredTransfer) {
  hostmem_multi_arena mem{};
  grdw_gradido_transaction tx{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_gradido_transaction_init(&tx);
  auto serializedTx = fromBase64(
      timeoutDeferredTransferTransactionBase64, strlen(timeoutDeferredTransferTransactionBase64)
  );
  ASSERT_EQ(
      grdw_gradido_transaction_decode(&tx, &serializedTx, &g_pbWorkspace, &mem), HOSTMEM_SUCCESS
  );
  free(serializedTx.data);
  EXPECT_FALSE(tx.sig_map_count);
  EXPECT_FALSE(tx.sig_map);
  EXPECT_EQ(tx.pairing_ledger_anchor.id, 17);
  EXPECT_EQ(tx.pairing_ledger_anchor.type, GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_ID);

  grdw_gradido_transaction_free(&tx, &mem);
  hostmem_multi_arena_release(&mem);
}

// ###############  Confirmed Transaction    ############################################
TEST(PBToolsTest, ConfirmedTransaction_Encode_CommunityRoot) {
  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE * 2, 0, nullptr), HOSTMEM_SUCCESS);

  grdw_confirmed_transaction tx;
  grdw_confirmed_transaction_init(&tx);
  tx.id = 121;
  tx.transaction.body_bytes =
      fromBase64(communityRootTransactionBodyBase64, strlen(communityRootTransactionBodyBase64));
  ASSERT_EQ(grdw_gradido_transaction_reserve_sig_map(&tx.transaction, 1, &mem), HOSTMEM_SUCCESS);
  grdw_signature_pair signature{};
  memcpy(signature.public_key, g_KeyPairs[0].public_key, 32);
  unsigned long long sig_len = 64;
  crypto_sign_detached(
      signature.signature, &sig_len, (const unsigned char *)communityRootTransactionBodyBase64,
      strlen(communityRootTransactionBodyBase64), g_KeyPairs[0].private_key
  );
  ASSERT_EQ(grdw_gradido_transaction_copy_sig_map(&tx.transaction, &signature, 0), HOSTMEM_SUCCESS);
  uint8_t buffer[512]{};
  hostmem_memory_block bufferPtr = {.data = buffer, .size = 512};
  int finalSize = 0;
  tx.confirmed_at = confirmedAt;
  crypto_generichash_state state;
  crypto_generichash_init(&state, nullptr, 0, crypto_generichash_BYTES);
  crypto_generichash_update(&state, tx.transaction.body_bytes.data, tx.transaction.body_bytes.size);
  crypto_generichash_final(&state, tx.running_hash, 32);
  grdw_ledger_anchor_assemble_hiero_transaction_id(
      &tx.ledger_anchor, createdAt2.seconds, createdAt2.nanos, 0, 0, 121
  );
  ASSERT_EQ(grdw_confirmed_transaction_reserve_account_balances(&tx, 1, &mem), HOSTMEM_SUCCESS);
  grdw_account_balance accountBalance;
  accountBalance.balance = 10000;
  memcpy(accountBalance.pubkey, g_KeyPairs[4].public_key, 32);
  hostmem_memory_block communityUuid = fromHex(communityUuidHex);
  memcpy(accountBalance.community_uuid, communityUuid.data, 16);
  free(communityUuid.data);
  ASSERT_EQ(
      grdw_confirmed_transaction_copy_account_balance(&tx, &accountBalance, 0), HOSTMEM_SUCCESS
  );
  tx.balance_derivation = GRDT_BALANCE_DERIVATION_EXTERN;

  ASSERT_EQ(
      grdw_confirmed_transaction_encode(&bufferPtr, &finalSize, &tx, &g_pbWorkspace),
      HOSTMEM_SUCCESS
  );
  bufferPtr.size = finalSize;
  auto base64 = toBase64(&bufferPtr);
  ASSERT_EQ(base64, confirmedCommunityRootTransactionBase64);
  // printf("complete confirmed community root tx: %s\n", base64.c_str());
  auto hex = toHex(&bufferPtr);
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  free(tx.transaction.body_bytes.data);
  tx.transaction.body_bytes.data = nullptr;
  grdw_confirmed_transaction_free(&tx, &mem);
  hostmem_multi_arena_release(&mem);
}

TEST(PBToolsTest, ConfirmedTransaction_Decode_CommunityRoot) {
  hostmem_multi_arena mem{};
  ASSERT_EQ(hostmem_multi_arena_init(&mem, BUFFER_SIZE * 2, 0, nullptr), HOSTMEM_SUCCESS);

  grdw_confirmed_transaction tx;
  grdw_confirmed_transaction_init(&tx);
  auto base64 = fromBase64(
      confirmedCommunityRootTransactionBase64, strlen(confirmedCommunityRootTransactionBase64)
  );
  ASSERT_EQ(grdw_confirmed_transaction_decode(&tx, &base64, &g_pbWorkspace, &mem), HOSTMEM_SUCCESS);
  EXPECT_EQ(tx.id, 121);
  auto bodyBytesBase64 = toBase64(&tx.transaction.body_bytes);
  EXPECT_EQ(bodyBytesBase64, communityRootTransactionBodyBase64);
  EXPECT_EQ(tx.confirmed_at.seconds, confirmedAt.seconds);
  EXPECT_EQ(tx.confirmed_at.nanos, confirmedAt.nanos);
  EXPECT_EQ(tx.account_balances_count, 1);
  EXPECT_EQ(tx.account_balances[0].balance, 10000);
  EXPECT_EQ(tx.ledger_anchor.type, GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID);
  EXPECT_EQ(
      tx.ledger_anchor.hiero_transaction_id.transactionValidStart.seconds, createdAt2.seconds
  );
  EXPECT_EQ(tx.ledger_anchor.hiero_transaction_id.transactionValidStart.nanos, createdAt2.nanos);
  EXPECT_EQ(tx.ledger_anchor.hiero_transaction_id.accountID.accountNum, 121);
  EXPECT_EQ(tx.balance_derivation, GRDT_BALANCE_DERIVATION_EXTERN);

  // everything the decode produced sits in the arena; only the base64 bytes came from malloc
  grdw_confirmed_transaction_free(&tx, &mem);
  free(base64.data);
  hostmem_multi_arena_release(&mem);
}
