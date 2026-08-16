#include "gradido_blockchain_core/utils/converter.h"
#include "hostmem/memory.h"
#include "hostmem/memory_block.h"
#include "hostmem/mono_timer.h"
#include <cstring>
#include <gtest/gtest.h>

#include "hostmem/converter.h"
#include "memory_limit.h"
#include <random>
#include <string>

#ifdef USE_SODIUM

TEST(UuidTest, RoundtripValidUuid) {
  // A known UUID
  const uint8_t original[16] = {0x48, 0x06, 0x6a, 0x47, 0xa0, 0x2f, 0x45, 0x96,
                                0x88, 0x3c, 0x30, 0x2c, 0x2b, 0x1a, 0xa1, 0xe1};
  const char expected[] = "48066a47-a02f-4596-883c-302c2b1aa1e1";

  // Forward: UUID → String
  char uuid_string[37];
  grdu_uuid_to_string(uuid_string, original);
  EXPECT_STREQ(uuid_string, expected);
  EXPECT_EQ(strlen(uuid_string), 36); // exactly 36 characters without null terminator

  // Backward: String → UUID
  uint8_t decoded[16];
  hostmem_result result = grdu_uuid_from_string(decoded, uuid_string);
  EXPECT_EQ(result, HOSTMEM_SUCCESS);
  EXPECT_EQ(memcmp(original, decoded, 16), 0);
}

TEST(UuidTest, InvalidInputs) {
  uint8_t uuid[16];

  // Null-Pointer
  EXPECT_EQ(
      grdu_uuid_from_string(nullptr, "48066a47-a02f-4596-883c-302c2b1aa1e1"),
      HOSTMEM_ERROR_NULL_POINTER
  );
  EXPECT_EQ(grdu_uuid_from_string(uuid, nullptr), HOSTMEM_ERROR_NULL_POINTER);

  // Wrong length
  EXPECT_EQ(grdu_uuid_from_string(uuid, "too-short"), HOSTMEM_ERROR_INVALID_PARAM);
  EXPECT_EQ(
      grdu_uuid_from_string(uuid, "48066a47-a02f-4596-883c-302c2b1aa1e1-extra"),
      HOSTMEM_ERROR_INVALID_PARAM
  );

  // Invalid hex characters
  EXPECT_EQ(
      grdu_uuid_from_string(uuid, "XXXX6a47-a02f-4596-883c-302c2b1aa1e1"),
      HOSTMEM_ERROR_DECODE_FAILED
  );
}

TEST(UuidTest, AllZeros) {
  const uint8_t zeros[16] = {0};
  char uuid_string[37];
  grdu_uuid_to_string(uuid_string, zeros);
  EXPECT_STREQ(uuid_string, "00000000-0000-0000-0000-000000000000");

  uint8_t decoded[16];
  EXPECT_EQ(grdu_uuid_from_string(decoded, uuid_string), HOSTMEM_SUCCESS);
  EXPECT_EQ(memcmp(zeros, decoded, 16), 0);
}

TEST(UuidTest, MultipleRoundtrips) {
  // Test several random UUIDs
  const char *test_uuids[] = {
      "123e4567-e89b-12d3-a456-426614174000",
      "00000000-0000-0000-0000-000000000000",
      "ffffffff-ffff-ffff-ffff-ffffffffffff",
      "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  };

  for (const auto &str : test_uuids) {
    uint8_t decoded[16];
    EXPECT_EQ(grdu_uuid_from_string(decoded, str), HOSTMEM_SUCCESS);

    char encoded[37];
    grdu_uuid_to_string(encoded, decoded);
    EXPECT_STREQ(encoded, str);
  }
}

#endif // USE_SODIUM

// INT64_MIN is the one value that cannot be negated in int64_t: `v * -1` is undefined there,
// and the result only looked right because two's complement wrapping happened to land on it.

#ifdef USE_SODIUM

// libsodium answers a destination that is too small by calling sodium_misuse(), which aborts
// the process — so the room has to be checked before the call, not from its return value. The
// NULL check that used to stand there could never fire.
TEST(Base64Test, DestinationTooSmallIsReportedInsteadOfFatal) {
  hostmem mem{};
  ASSERT_EQ(hostmem_init_arena(&mem, 4096), HOSTMEM_SUCCESS);

  uint8_t payload[32];
  for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<uint8_t>(i);
  hostmem_memory_block data{payload, sizeof(payload)};

  const size_t needed = grdu_binary_to_base64_length(sizeof(payload));
  ASSERT_GT(needed, 1u);

  // one byte short: reported, not fatal
  hostmem_memory_block tooSmall{};
  ASSERT_EQ(
      hostmem_memory_block_alloc(&tooSmall, static_cast<uint32_t>(needed - 1), &mem),
      HOSTMEM_SUCCESS
  );
  EXPECT_EQ(
      grdu_binary_to_base64_with_known_size(&tooSmall, &data),
      HOSTMEM_ERROR_DESTINATION_BUFFER_TO_SMALL
  );

  // exactly enough: encodes, and the string is terminated where it should be
  hostmem_memory_block exact{};
  ASSERT_EQ(
      hostmem_memory_block_alloc(&exact, static_cast<uint32_t>(needed), &mem), HOSTMEM_SUCCESS
  );
  ASSERT_EQ(grdu_binary_to_base64_with_known_size(&exact, &data), HOSTMEM_SUCCESS);
  EXPECT_EQ(strlen(reinterpret_cast<char *>(exact.data)), needed - 1);

  hostmem_release(&mem);
}

// a missing pointer and an empty block are different mistakes and say so
TEST(Base64Test, HexRejectsNullAndEmptySeparately) {
  uint8_t payload[4] = {1, 2, 3, 4};
  char out[16];
  hostmem_memory_block data{payload, sizeof(payload)};
  hostmem_memory_block empty{payload, 0};

  EXPECT_EQ(grdu_binary_to_hex(nullptr, &data), HOSTMEM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdu_binary_to_hex(out, nullptr), HOSTMEM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdu_binary_to_hex(out, &empty), HOSTMEM_ERROR_INVALID_PARAM);
  EXPECT_EQ(grdu_binary_to_hex(out, &data), HOSTMEM_SUCCESS);
}

#endif // USE_SODIUM
