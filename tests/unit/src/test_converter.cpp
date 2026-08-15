#include "gradido_blockchain_core/utils/converter.h"
#include "hostmem/mono_timer.h"
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
