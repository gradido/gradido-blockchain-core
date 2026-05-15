#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/utils/mono_timer.h"
#include "gradido_blockchain_core/utils/version.h"
#include "key_pairs.h"

#include "sodium.h"
#include <assert.h>
#include <gtest/gtest.h>
#include <string.h>

// Transaction Body
// contain only createAt and version string
constexpr auto emptyTransactionBodyBase64 = "CgASCAiAzLn/BRAAGgMzLjMgAA==";
constexpr auto communityRootTransactionBodyBase64 =
    "WmYKIIFnAymUaYjt9FH0xCRpHYPPWpBDkEKILVu3IkPvVR70EiDX46igkKpEhzJG9cas/Bf/dO4XT1bnvSpV/"
    "7gQQfbbHRogrYcHSiqkvALTeX+7Q8iyvm7dbLHWNEqUD8UovOHhMLISBgiAzLn/BRiIgAw=";
constexpr auto registerAddressTransactionBodyBase64 =
    "SmoKIPTdOYn3VUt6sy490Lf54Rr86QoYEenR9ncWnrRL9EJyEAEaIGPNFY6lZapL/"
    "RYfuH7yyZBvPXAPm+BzMmX+h3Mv9TcQIiDbDtYSWhTwMKvtG/yDHgohjPn6v87n7NWBwMDniPAXxygBEgYIgMy5/"
    "wUYiIAM";
constexpr auto creationTransactionBodyBase64 = "OkMKOQog2w7WEloU8DCr7Rv8gx4KIYz5+r/"
                                               "O5+"
                                               "zVgcDA54jwF8cQgK3iBBoQAZ4sMaMDdcCUHvNcWeT5eBoGCPD9u"
                                               "P8FChEIAhINSGVsbG8gV29ybGQyABIGCIDMuf8FGIiADA==";
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
constexpr auto registeAddressTransactionBase64 =
    "CrICCmQKIKrRK6gmuRFEV1VMTFV/"
    "o6h4r58h+BYhsMPmFhUGESgpEkDdBYnw6FWxdQXZ+"
    "K9cnXB3JDJFQT8KVhC1yqgnTKNuLgjiyxK6K9vzZEeFHiE6wXoxQ7o4wVPLnEd8QVItATQHCmQKILxxJbfkt7U00jLCFcL"
    "LzjRAiGBKDoruOTLpv0FqBs/+EkDc28tzP3cvgcwM4xCEktvEhPw0ikUb2xTSUa3zG5N5dKxMJ5GPJS/"
    "n78pLejfjQ8OiVm8NP7LP9YXZAyHlT8MFCmQKIO/ykZNQpTrID4mAYrfGf/gCgkX0zETgjvIviFFxVU87EkC4J/"
    "ByDZPNlb6Jgi39rH+B3YgbgITV2kL0S6HmspFymCouglWwR24ym7+c6J0CSfOyFTiSNAqLhzsGxCmc6doOEnkSBgiAzLn/"
    "BRoDMy41SmoKILxxJbfkt7U00jLCFcLLzjRAiGBKDoruOTLpv0FqBs/+EAEaIB/"
    "kH3lmpLYaljiGoPpNE22FeUJNnAKidR77oOjlVZ9hIiDv8pGTUKU6yA+JgGK3xn/4AoJF9MxE4I7yL4hRcVVPOygBGgA=";
constexpr auto creationTransactionBase64 =
    "CmYKZAogyy3XdFIKQdmOo6znQO6zP6F5nXcyrbd24930wGPWAbwSQLoWkuWdqFViwdGaETYuCAGtASEcfNc9dMocRAylm7"
    "S73bqNYsNuuqCp9fQeWNG4ZpIgO5ksd4b9Xq1gkKXV8wESXwodCAISGURlaW5lIGVyc3RlIFNjaG9lcGZ1bmcgOykSBgiA"
    "zLn/BRoDMy41OjEKJwog7/KRk1ClOsgPiYBit8Z/+AKCRfTMROCO8i+IUXFVTzsQgK3iBBoGCLjKuf8FGgA=";
constexpr auto transferTransactionBase64 =
    "CmYKZAog7/KRk1ClOsgPiYBit8Z/+AKCRfTMROCO8i+IUXFVTzsSQE77i3/OM2356rKFJYaktfYQpVTj3a0U/"
    "D+G5rYQ9L8mcU+OtSYG0sx/LLtBDB9gn/"
    "9Pu6NlHkVLib4fj5BXywAScQoVCAISEUljaCB0ZWlsZSBtaXQgZGlyEgYIgMy5/wUaAzMuNTJLCicKIO/"
    "ykZNQpTrID4mAYrfGf/gCgkX0zETgjvIviFFxVU87ELzBsQISIH71fuNOTK5eAUA0t/"
    "gEO6ZA0aJvB9YirDQuZK11ufYNGgA=";
constexpr auto deferredTransferTransactionBase64 =
    "CmYKZAog7/KRk1ClOsgPiYBit8Z/+AKCRfTMROCO8i+IUXFVTzsSQLNxKNCE73Y/"
    "6R6DCWPghfgrHGyI3j7SaDTDbRq2EJbrcDpMQbU7zNUffvatlheOJltUXy8++KFI0GFy/"
    "0JMZAsSewoWCAISEkxpbmsgenVtIGVpbmxvZXNlbhIGCIDMuf8FGgMzLjVSVApLCicKIO/ykZNQpTrID4mAYrfGf/"
    "gCgkX0zETgjvIviFFxVU87EKyK0wISIH71fuNOTK5eAUA0t/gEO6ZA0aJvB9YirDQuZK11ufYNEgUI1sLhAxoA";
constexpr auto communityFriendsUpdateBase64 =
    "CmYKZAogqtErqCa5EURXVUxMVX+jqHivnyH4FiGww+"
    "YWFQYRKCkSQP7aJIvxwxyoUFMAOXQsI5tBWJtr5RxDMiUKJKNUOxJsY3aR8z16kzuYamk9/"
    "kPuMVS8NHCEzVcbEFCqQTAqYQoSERIGCIDMuf8FGgMzLjVCAggBGgA=";

constexpr auto communityUuidHex = "019e2c31a30375c0941ef35c59e4f978";

grdw_timestamp createdAt1 = {.seconds = 1609459200, .nanos = 0};
grdw_timestamp createdAt2 = {.seconds = 1609459200, .nanos = 281271};
grdw_timestamp_seconds targetDate = {.seconds = 1609449200};

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

static std::string toBase64(grd_memory_block *data, int variant = sodium_base64_VARIANT_ORIGINAL) {
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

static std::string toHex(grd_memory_block *data) {
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

static grd_memory_block fromHex(const char *hex) {
  grd_memory_block result{};
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
  grd_memory mem;
  grdw_transaction_body body{};
  auto bin = fromBase64(emptyTransactionBodyBase64, strlen(emptyTransactionBodyBase64));
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  grdw_transaction_body_decode(&body, &bin, &mem);
  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_CommunityRoot_Encode) {
  init_key_pairs();
  grd_memory mem;
  ASSERT_EQ(grd_memory_init_arena(&mem, 128), GRD_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.created_at = createdAt1;
  memcpy(body.community_root.pubkey, g_KeyPairs[0].public_key, 32);
  memcpy(body.community_root.gmw_pubkey, g_KeyPairs[1].public_key, 32);
  memcpy(body.community_root.auf_pubkey, g_KeyPairs[2].public_key, 32);
  body.transaction_type = GRDD_TRANSACTION_TYPE_COMMUNITY_ROOT;
  uint8_t buffer[256]{};
  grd_memory_block bufferPtr = {.data = buffer, .size = 256};
  size_t finalSize = 0;
  ASSERT_EQ(grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &mem), GRD_SUCCESS);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  EXPECT_STREQ(base64.data(), communityRootTransactionBodyBase64);
  // printf("community root:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());

  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_CommunityRoot_Decode) {
  grd_memory mem;
  grdw_transaction_body body{};
  auto bin =
      fromBase64(communityRootTransactionBodyBase64, strlen(communityRootTransactionBodyBase64));
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &mem), GRD_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt1.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt1.nanos);
  EXPECT_EQ(body.type, GRDD_CROSS_GROUP_TYPE_LOCAL);
  EXPECT_FALSE(body.memos);
  EXPECT_FALSE(body.memos_count);
  EXPECT_EQ(body.transaction_type, GRDD_TRANSACTION_TYPE_COMMUNITY_ROOT);
  EXPECT_FALSE(memcmp(body.community_root.pubkey, g_KeyPairs[0].public_key, 32));
  EXPECT_FALSE(memcmp(body.community_root.gmw_pubkey, g_KeyPairs[1].public_key, 32));
  EXPECT_FALSE(memcmp(body.community_root.auf_pubkey, g_KeyPairs[2].public_key, 32));
  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_RegisterAddress_Encode) {
  init_key_pairs();
  grd_memory mem;
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.created_at = createdAt1;
  memcpy(body.register_address.user_pubkey, g_KeyPairs[3].public_key, 32);
  memcpy(body.register_address.account_pubkey, g_KeyPairs[4].public_key, 32);
  body.register_address.address_type = GRDD_ADDRESS_TYPE_COMMUNITY_HUMAN;
  body.register_address.derivation_index = 1;
  crypto_generichash(body.register_address.name_hash, 32, g_KeyPairs[3].public_key, 32, NULL, 0);
  body.transaction_type = GRDD_TRANSACTION_TYPE_REGISTER_ADDRESS;
  uint8_t buffer[256]{};
  grd_memory_block bufferPtr = {.data = buffer, .size = 256};
  size_t finalSize = 0;
  ASSERT_EQ(grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &mem), GRD_SUCCESS);
  // printf("finalSize: %lld\n", finalSize);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  EXPECT_EQ(base64, registerAddressTransactionBodyBase64);
  // printf("register address:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());

  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_RegisterAddress_Decode) {
  grd_memory mem;
  grdw_transaction_body body{};
  auto bin = fromBase64(
      registerAddressTransactionBodyBase64, strlen(registerAddressTransactionBodyBase64)
  );
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &mem), GRD_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt1.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt1.nanos);
  EXPECT_EQ(body.type, GRDD_CROSS_GROUP_TYPE_LOCAL);
  EXPECT_FALSE(body.memos);
  EXPECT_FALSE(body.memos_count);
  EXPECT_EQ(body.transaction_type, GRDD_TRANSACTION_TYPE_REGISTER_ADDRESS);
  EXPECT_FALSE(memcmp(body.register_address.user_pubkey, g_KeyPairs[3].public_key, 32));
  EXPECT_FALSE(memcmp(body.register_address.account_pubkey, g_KeyPairs[4].public_key, 32));
  EXPECT_EQ(body.register_address.address_type, GRDD_ADDRESS_TYPE_COMMUNITY_HUMAN);
  EXPECT_EQ(body.register_address.derivation_index, 1);
  EXPECT_EQ(crypto_generichash_BYTES, 32);
  uint8_t nameHash[crypto_generichash_BYTES];
  crypto_generichash(nameHash, crypto_generichash_BYTES, g_KeyPairs[3].public_key, 32, NULL, 0);
  EXPECT_FALSE(memcmp(body.register_address.name_hash, nameHash, 32));
  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_Creation_Encode) {
  init_key_pairs();
  grd_memory mem;
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  grdw_transaction_body_reserve_memos(&body, 1, &mem);
  grdw_encrypted_memo memo = {.type = GRDW_MEMO_KEY_TYPE_PLAIN};
  grd_memory_block_alloc(&memo.memo, &mem, 13);
  assert(memo.memo.data);
  memcpy(memo.memo.data, "Hello World2", 13);
  grdw_transaction_body_move_memo(&body, &memo, 0);
  body.created_at = createdAt1;
  memcpy(body.creation.recipient.pubkey, g_KeyPairs[4].public_key, 32);
  body.creation.recipient.amount = 10000000;
  grd_memory_block communityUuid = fromHex(communityUuidHex);
  memcpy(body.creation.recipient.community_uuid, communityUuid.data, 16);
  free(communityUuid.data);
  body.creation.target_date = targetDate;
  body.transaction_type = GRDD_TRANSACTION_TYPE_CREATION;
  uint8_t buffer[256]{};
  grd_memory_block bufferPtr = {.data = buffer, .size = 256};
  size_t finalSize = 0;
  ASSERT_EQ(grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &mem), GRD_SUCCESS);
  // printf("finalSize: %lld\n", finalSize);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  // printf("creation:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  EXPECT_EQ(base64, creationTransactionBodyBase64);
  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_Creation_Decode) {
  grd_memory mem;
  grdw_transaction_body body{};
  auto bin = fromBase64(creationTransactionBodyBase64, strlen(creationTransactionBodyBase64));
  EXPECT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &mem), GRD_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt1.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt1.nanos);
  EXPECT_EQ(body.type, GRDD_CROSS_GROUP_TYPE_LOCAL);
  EXPECT_TRUE(body.memos);
  EXPECT_EQ(body.memos_count, 1);
  EXPECT_EQ(body.memos[0].type, GRDW_MEMO_KEY_TYPE_PLAIN);
  EXPECT_STREQ((const char *)body.memos[0].memo.data, "Hello World2");
  EXPECT_EQ(body.transaction_type, GRDD_TRANSACTION_TYPE_CREATION);
  EXPECT_FALSE(memcmp(body.creation.recipient.pubkey, g_KeyPairs[4].public_key, 32));
  EXPECT_EQ(body.creation.recipient.amount, 10000000);
  grd_memory_block communityUuid = fromHex(communityUuidHex);
  EXPECT_FALSE(memcmp(body.creation.recipient.community_uuid, communityUuid.data, 16));
  free(communityUuid.data);
  EXPECT_EQ(body.creation.target_date.seconds, targetDate.seconds);
  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_Transfer_Encode) {
  init_key_pairs();
  grd_memory mem;
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  grdw_transaction_body_reserve_memos(&body, 1, &mem);
  grdw_encrypted_memo memo = {.type = GRDW_MEMO_KEY_TYPE_PLAIN};
  grd_memory_block_alloc(&memo.memo, &mem, 13);
  assert(memo.memo.data);
  memcpy(memo.memo.data, "Hello World2", 13);
  grdw_transaction_body_move_memo(&body, &memo, 0);
  body.created_at = createdAt2;
  memcpy(body.transfer.sender.pubkey, g_KeyPairs[4].public_key, 32);
  body.transfer.sender.amount = 100000;
  grd_memory_block communityUuid = fromHex(communityUuidHex);
  memcpy(body.transfer.sender.community_uuid, communityUuid.data, 16);
  free(communityUuid.data);
  memcpy(body.transfer.recipient, g_KeyPairs[5].public_key, 32);

  body.transaction_type = GRDD_TRANSACTION_TYPE_TRANSFER;
  uint8_t buffer[256]{};
  grd_memory_block bufferPtr = {.data = buffer, .size = 256};
  size_t finalSize = 0;
  ASSERT_EQ(grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &mem), GRD_SUCCESS);
  // printf("finalSize: %lld\n", finalSize);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  // printf("transfer:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  EXPECT_EQ(base64, transferTransactionBodyBase64);
  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_Transfer_Decode) {
  grd_memory mem;
  grdw_transaction_body body{};
  auto bin = fromBase64(transferTransactionBodyBase64, strlen(transferTransactionBodyBase64));
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &mem), GRD_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt2.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt2.nanos);
  EXPECT_EQ(body.type, GRDD_CROSS_GROUP_TYPE_LOCAL);
  EXPECT_TRUE(body.memos);
  EXPECT_EQ(body.memos_count, 1);
  EXPECT_EQ(body.memos[0].type, GRDW_MEMO_KEY_TYPE_PLAIN);
  EXPECT_STREQ((const char *)body.memos[0].memo.data, "Hello World2");
  EXPECT_EQ(body.transaction_type, GRDD_TRANSACTION_TYPE_TRANSFER);
  EXPECT_FALSE(memcmp(body.transfer.sender.pubkey, g_KeyPairs[4].public_key, 32));
  EXPECT_EQ(body.transfer.sender.amount, 100000);
  grd_memory_block communityUuid = fromHex(communityUuidHex);
  EXPECT_FALSE(memcmp(body.transfer.sender.community_uuid, communityUuid.data, 16));
  free(communityUuid.data);
  EXPECT_FALSE(memcmp(body.transfer.recipient, g_KeyPairs[5].public_key, 32));
  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_Deferred_Transfer_Encode) {
  init_key_pairs();
  grd_memory mem;
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  grdw_transaction_body_reserve_memos(&body, 1, &mem);
  grdw_encrypted_memo memo = {.type = GRDW_MEMO_KEY_TYPE_PLAIN};
  grd_memory_block_alloc(&memo.memo, &mem, 13);
  assert(memo.memo.data);
  memcpy(memo.memo.data, "Hello World2", 13);
  grdw_transaction_body_move_memo(&body, &memo, 0);
  body.created_at = createdAt2;
  memcpy(body.deferred_transfer.transfer.sender.pubkey, g_KeyPairs[4].public_key, 32);
  body.deferred_transfer.transfer.sender.amount = 100000;
  grd_memory_block communityUuid = fromHex(communityUuidHex);
  memcpy(body.deferred_transfer.transfer.sender.community_uuid, communityUuid.data, 16);
  free(communityUuid.data);
  memcpy(body.deferred_transfer.transfer.recipient, g_KeyPairs[5].public_key, 32);
  body.deferred_transfer.timeout_duration = 17261;

  body.transaction_type = GRDD_TRANSACTION_TYPE_DEFERRED_TRANSFER;
  uint8_t buffer[256]{};
  grd_memory_block bufferPtr = {.data = buffer, .size = 256};
  size_t finalSize = 0;
  ASSERT_EQ(grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &mem), GRD_SUCCESS);
  // printf("finalSize: %lld\n", finalSize);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  // printf("deferred transfer:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  EXPECT_EQ(base64, deferredTransferTransactionBodyBase64);
  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_Deferred_Transfer_Decode) {
  grd_memory mem;
  grdw_transaction_body body{};
  auto bin = fromBase64(
      deferredTransferTransactionBodyBase64, strlen(deferredTransferTransactionBodyBase64)
  );
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &mem), GRD_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt2.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt2.nanos);
  EXPECT_EQ(body.type, GRDD_CROSS_GROUP_TYPE_LOCAL);
  EXPECT_TRUE(body.memos);
  EXPECT_EQ(body.memos_count, 1);
  EXPECT_EQ(body.memos[0].type, GRDW_MEMO_KEY_TYPE_PLAIN);
  EXPECT_STREQ((const char *)body.memos[0].memo.data, "Hello World2");
  EXPECT_EQ(body.transaction_type, GRDD_TRANSACTION_TYPE_DEFERRED_TRANSFER);
  EXPECT_FALSE(memcmp(body.deferred_transfer.transfer.sender.pubkey, g_KeyPairs[4].public_key, 32));
  EXPECT_EQ(body.deferred_transfer.transfer.sender.amount, 100000);
  grd_memory_block communityUuid = fromHex(communityUuidHex);
  EXPECT_FALSE(
      memcmp(body.deferred_transfer.transfer.sender.community_uuid, communityUuid.data, 16)
  );
  free(communityUuid.data);
  EXPECT_FALSE(memcmp(body.deferred_transfer.transfer.recipient, g_KeyPairs[5].public_key, 32));
  EXPECT_EQ(body.deferred_transfer.timeout_duration, 17261);
  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_Redeem_Deferred_Transfer_Encode) {
  init_key_pairs();
  grd_memory mem;
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  grdw_transaction_body_reserve_memos(&body, 1, &mem);
  grdw_encrypted_memo memo = {.type = GRDW_MEMO_KEY_TYPE_PLAIN};
  grd_memory_block_alloc(&memo.memo, &mem, 13);
  assert(memo.memo.data);
  memcpy(memo.memo.data, "Hello World2", 13);
  grdw_transaction_body_move_memo(&body, &memo, 0);
  body.created_at = createdAt2;
  memcpy(body.redeem_deferred_transfer.transfer.sender.pubkey, g_KeyPairs[4].public_key, 32);
  body.redeem_deferred_transfer.transfer.sender.amount = 100000;
  grd_memory_block communityUuid = fromHex(communityUuidHex);
  memcpy(body.redeem_deferred_transfer.transfer.sender.community_uuid, communityUuid.data, 16);
  free(communityUuid.data);
  memcpy(body.redeem_deferred_transfer.transfer.recipient, g_KeyPairs[5].public_key, 32);
  body.redeem_deferred_transfer.deferred_transfer_transaction_nr = 15;

  body.transaction_type = GRDD_TRANSACTION_TYPE_REDEEM_DEFERRED_TRANSFER;
  uint8_t buffer[256]{};
  grd_memory_block bufferPtr = {.data = buffer, .size = 256};
  size_t finalSize = 0;
  ASSERT_EQ(grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &mem), GRD_SUCCESS);
  // printf("finalSize: %lld\n", finalSize);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  // printf("redeem deferred transfer:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  EXPECT_EQ(base64, redeemDeferredTransferTransactionBodyBase64);
  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_Redeem_Deferred_Transfer_Decode) {
  grd_memory mem;
  grdw_transaction_body body{};
  auto bin = fromBase64(
      redeemDeferredTransferTransactionBodyBase64,
      strlen(redeemDeferredTransferTransactionBodyBase64)
  );
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &mem), GRD_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt2.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt2.nanos);
  EXPECT_EQ(body.type, GRDD_CROSS_GROUP_TYPE_LOCAL);
  EXPECT_TRUE(body.memos);
  EXPECT_EQ(body.memos_count, 1);
  EXPECT_EQ(body.memos[0].type, GRDW_MEMO_KEY_TYPE_PLAIN);
  EXPECT_STREQ((const char *)body.memos[0].memo.data, "Hello World2");
  EXPECT_EQ(body.transaction_type, GRDD_TRANSACTION_TYPE_REDEEM_DEFERRED_TRANSFER);
  EXPECT_FALSE(
      memcmp(body.redeem_deferred_transfer.transfer.sender.pubkey, g_KeyPairs[4].public_key, 32)
  );
  EXPECT_EQ(body.redeem_deferred_transfer.transfer.sender.amount, 100000);
  grd_memory_block communityUuid = fromHex(communityUuidHex);
  EXPECT_FALSE(
      memcmp(body.redeem_deferred_transfer.transfer.sender.community_uuid, communityUuid.data, 16)
  );
  free(communityUuid.data);
  EXPECT_FALSE(
      memcmp(body.redeem_deferred_transfer.transfer.recipient, g_KeyPairs[5].public_key, 32)
  );
  EXPECT_EQ(body.redeem_deferred_transfer.deferred_transfer_transaction_nr, 15);
  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_Timeout_Deferred_Transfer_Encode) {
  init_key_pairs();
  grd_memory mem;
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.created_at = createdAt2;
  body.timeout_deferred_transfer.deferred_transfer_transaction_nr = 16;

  body.transaction_type = GRDD_TRANSACTION_TYPE_TIMEOUT_DEFERRED_TRANSFER;
  uint8_t buffer[256]{};
  grd_memory_block bufferPtr = {.data = buffer, .size = 256};
  size_t finalSize = 0;
  ASSERT_EQ(grdw_transaction_body_encode(&bufferPtr, &finalSize, &body, &mem), GRD_SUCCESS);
  // printf("finalSize: %lld\n", finalSize);
  bufferPtr.size = finalSize;
  auto hex = toHex(&bufferPtr);
  auto base64 = toBase64(&bufferPtr);
  // printf("timeout deferred transfer:\n%s\n", base64.c_str());
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  EXPECT_EQ(base64, timeoutDeferredTransferTransactionBodyBase64);
  grd_memory_free(&mem);
}

TEST(PBToolsTest, TransactionBody_Timeout_Deferred_Transfer_Decode) {
  grd_memory mem;
  grdw_transaction_body body{};
  auto bin = fromBase64(
      timeoutDeferredTransferTransactionBodyBase64,
      strlen(timeoutDeferredTransferTransactionBodyBase64)
  );
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  ASSERT_EQ(grdw_transaction_body_decode(&body, &bin, &mem), GRD_SUCCESS);
  free(bin.data);
  EXPECT_EQ(body.created_at.seconds, createdAt2.seconds);
  EXPECT_EQ(body.created_at.nanos, createdAt2.nanos);
  EXPECT_EQ(body.type, GRDD_CROSS_GROUP_TYPE_LOCAL);
  EXPECT_FALSE(body.memos);
  EXPECT_FALSE(body.memos_count);

  EXPECT_EQ(body.transaction_type, GRDD_TRANSACTION_TYPE_TIMEOUT_DEFERRED_TRANSFER);
  EXPECT_EQ(body.timeout_deferred_transfer.deferred_transfer_transaction_nr, 16);
  grd_memory_free(&mem);
}

// ###############    Gradido Transaction    ############################################

TEST(PBToolsTest, GradidoTransaction_Encode_CommunityRoot) {
  grd_memory mem;
  grdw_gradido_transaction tx{};
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  grdw_gradido_transaction_init(&tx);
  tx.body_bytes =
      fromBase64(communityRootTransactionBodyBase64, strlen(communityRootTransactionBodyBase64));
  grdw_gradido_transaction_reserve_sig_map(&tx, 1, &mem);
  grdw_signature_pair signature{};
  memcpy(signature.public_key, g_KeyPairs[0].public_key, 32);
  size_t sig_len = 64;
  crypto_sign_detached(
      signature.signature, &sig_len, (const unsigned char *)communityRootTransactionBodyBase64,
      strlen(communityRootTransactionBodyBase64), g_KeyPairs[0].private_key
  );
  ASSERT_EQ(grdw_gradido_transaction_copy_sig_map(&tx, &signature, 0), GRD_SUCCESS);
  uint8_t buffer[256]{};
  grd_memory_block bufferPtr = {.data = buffer, .size = 256};
  size_t finalSize = 0;
  ASSERT_EQ(grdw_gradido_transaction_encode(&bufferPtr, &finalSize, &tx, &mem), GRD_SUCCESS);
  bufferPtr.size = finalSize;
  auto base64 = toBase64(&bufferPtr);
  ASSERT_EQ(base64, communityRootTransactionBase64);
  // printf("signed community root tx: %s\n", base64.c_str());
  auto hex = toHex(&bufferPtr);
  // printf("xxd -r -ps <<< \"%s\" | protoscope\n", hex.c_str());
  free(tx.body_bytes.data);
  tx.body_bytes.data = nullptr;
  grdw_gradido_transaction_free(&tx, &mem);
}

TEST(PBToolsTest, GradidoTransaction_Decode_CommunityRoot_1000X) {
  grdu_mono_timer timeUsed;
  grd_memory mem;
  grdw_gradido_transaction tx{};
  grdw_gradido_transaction_init(&tx);

  grdw_transaction_body body{};
  grdw_transaction_body_init(&body);

  grdu_mono_timer_reset(&timeUsed);
  auto bin = fromBase64(communityRootTransactionBase64, strlen(communityRootTransactionBase64));
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  char buffer[256];
  grdu_mono_timer_reset(&timeUsed);
  int i = 0;
  for (; i < 1000; ++i) {
    grd_memory_reset(&mem);

    ASSERT_EQ(grdw_gradido_transaction_decode(&tx, &bin, &mem), GRD_SUCCESS);
    ASSERT_EQ(grdw_transaction_body_decode(&body, &tx.body_bytes, &mem), GRD_SUCCESS);

    grdw_transaction_body_free(&body, &mem);
    grdw_gradido_transaction_free(&tx, &mem);
  }
  free(bin.data);

  grdu_mono_timer_string(buffer, 256, timeUsed);
  printf("time for decode community root %d times: %s\n", i, buffer);
  grd_memory_free(&mem);
}

TEST(PBToolsTest, GradidoTransaction_Encode_CommunityRoot_1000X) {
  grdu_mono_timer timeUsed;
  grd_memory mem;

  grdw_gradido_transaction tx{};
  ASSERT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  grdw_gradido_transaction_init(&tx);
  // tx.body_bytes = fromBase64(communityRootTransactionBodyBase64,
  // strlen(communityRootTransactionBodyBase64));
  grdw_gradido_transaction_reserve_sig_map(&tx, 1, &mem);
  grdw_signature_pair signature{};
  memcpy(signature.public_key, g_KeyPairs[0].public_key, 32);
  size_t sig_len = 64;
  crypto_sign_detached(
      signature.signature, &sig_len, (const unsigned char *)communityRootTransactionBodyBase64,
      strlen(communityRootTransactionBodyBase64), g_KeyPairs[0].private_key
  );
  ASSERT_EQ(grdw_gradido_transaction_copy_sig_map(&tx, &signature, 0), GRD_SUCCESS);

  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.created_at = createdAt1;
  memcpy(body.community_root.pubkey, g_KeyPairs[0].public_key, 32);
  memcpy(body.community_root.gmw_pubkey, g_KeyPairs[1].public_key, 32);
  memcpy(body.community_root.auf_pubkey, g_KeyPairs[2].public_key, 32);
  body.transaction_type = GRDD_TRANSACTION_TYPE_COMMUNITY_ROOT;

  uint8_t buffer[256]{};
  grd_memory_block bufferPtr = {.data = buffer, .size = 256};
  size_t finalSize = 0;
  grd_memory mem2;
  ASSERT_EQ(grd_memory_init_arena(&mem2, 1024), GRD_SUCCESS);
  int i = 0;
  grdu_mono_timer_reset(&timeUsed);
  for (; i < 1000; ++i) {
    grd_memory_reset(&mem2);
    grd_memory_block_alloc(&tx.body_bytes, &mem2, 256);
    ASSERT_EQ(grdw_transaction_body_encode(&tx.body_bytes, &finalSize, &body, &mem2), GRD_SUCCESS);
    tx.body_bytes.size = finalSize;
    ASSERT_EQ(grdw_gradido_transaction_encode(&bufferPtr, &finalSize, &tx, &mem2), GRD_SUCCESS);
  }
  char timerBuffer[256];
  grdu_mono_timer_string(timerBuffer, 256, timeUsed);
  printf("time for encode community root %d times: %s\n", i, timerBuffer);
  bufferPtr.size = finalSize;
  free(tx.body_bytes.data);
  tx.body_bytes.data = nullptr;
  grdw_gradido_transaction_free(&tx, &mem);
}
