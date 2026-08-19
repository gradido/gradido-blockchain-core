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

// promise: a string of the documented length whose separators are missing or sit elsewhere is
// rejected. It used to be accepted, and every absent separator turned two more characters into
// an output byte -- the first case below wrote 18 bytes into the 16 the caller owns, which
// AddressSanitizer reports as a heap buffer overflow. The guard bytes here fail the same way
// without a sanitizer.
TEST(UuidTest, SeparatorsMustSitWhereTheFormatSaysAndNeverOverrun) {
  struct {
    const char *what;
    const char *input;
  } const cases[] = {
      {"no separators at all", "48066a47a02f4596883c302c2b1aa1e1abcd"},
      {"two separators short", "48066a47a02f4596-883c-302c2b1aa1e1ab"},
      {"separators only", "------------------------------------"},
      {"first separator one group early", "4806-6a47a02f-4596-883c-302c2b1aa1e1"},
      {"a digit where the last separator belongs", "48066a47-a02f-4596-883cf302c2b1aa1e1"},
  };

  for (const auto &c : cases) {
    uint8_t guarded[32];
    memset(guarded, 0xCD, sizeof(guarded));

    EXPECT_EQ(grdu_uuid_from_string(guarded, c.input), HOSTMEM_ERROR_DECODE_FAILED) << c.what;

    for (size_t i = 16; i < sizeof(guarded); ++i) {
      EXPECT_EQ(guarded[i], 0xCD) << c.what << ": wrote past the 16 bytes it was given, at " << i;
    }
    // a rejected string leaves no half decoded bytes behind
    for (size_t i = 0; i < 16; ++i) { EXPECT_EQ(guarded[i], 0) << c.what; }
  }
}

// promise: every byte value survives the round trip, at every one of the 16 positions, in both
// digit cases -- the lookup tables both directions now use are indexed by the data itself, so a
// wrong entry would show up only for the values that reach it.
TEST(UuidTest, RoundTripCoversEveryByteValueAndBothCases) {
  for (unsigned value = 0; value < 256; ++value) {
    for (size_t position = 0; position < 16; ++position) {
      uint8_t original[16];
      memset(original, 0x5A, sizeof(original));
      original[position] = static_cast<uint8_t>(value);

      char text[37];
      grdu_uuid_to_string(text, original);
      ASSERT_EQ(strlen(text), 36u);
      ASSERT_EQ(text[8], '-');
      ASSERT_EQ(text[13], '-');
      ASSERT_EQ(text[18], '-');
      ASSERT_EQ(text[23], '-');

      uint8_t decoded[16];
      ASSERT_EQ(grdu_uuid_from_string(decoded, text), HOSTMEM_SUCCESS) << text;
      ASSERT_EQ(memcmp(original, decoded, 16), 0) << text;

      char upper[37];
      memcpy(upper, text, sizeof(upper));
      for (size_t i = 0; i < 36; ++i) {
        if (upper[i] >= 'a' && upper[i] <= 'f') { upper[i] = static_cast<char>(upper[i] - 32); }
      }
      uint8_t decoded_upper[16];
      ASSERT_EQ(grdu_uuid_from_string(decoded_upper, upper), HOSTMEM_SUCCESS) << upper;
      ASSERT_EQ(memcmp(original, decoded_upper, 16), 0) << upper;
    }
  }
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

// the reference the hex conversions used to be built on; still linked here, so the tests can
// check the hand written tables against it rather than against another copy of themselves
#include "sodium.h"

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

// promise: the lookup tables agree with libsodium, which is what both directions were built on
// until the tables replaced them. Every length from one byte up covers the loop's edges, and
// every byte value covers the table itself -- a wrong entry shows only for the values that
// reach it, so a handful of random samples would not find it.
TEST(HexTest, MatchesLibsodiumForEveryByteValueAndLength) {
  for (unsigned value = 0; value < 256; ++value) {
    uint8_t payload[64];
    for (size_t i = 0; i < sizeof(payload); ++i) {
      payload[i] = static_cast<uint8_t>((value + i * 7u) & 0xFFu);
    }
    payload[0] = static_cast<uint8_t>(value);

    for (size_t length = 1; length <= sizeof(payload); ++length) {
      hostmem_memory_block block{payload, static_cast<uint32_t>(length)};

      char ours[sizeof(payload) * 2 + 1];
      char reference[sizeof(payload) * 2 + 1];
      ASSERT_EQ(grdu_binary_to_hex(ours, &block), HOSTMEM_SUCCESS);
      sodium_bin2hex(reference, sizeof(reference), payload, length);
      ASSERT_STREQ(ours, reference) << "value " << value << " length " << length;

      uint8_t decoded[sizeof(payload)];
      ASSERT_EQ(grdu_binary_from_hex(decoded, ours), HOSTMEM_SUCCESS);
      ASSERT_EQ(memcmp(decoded, payload, length), 0) << "value " << value << " length " << length;
    }
  }
}

// promise: upper case digits decode to the same bytes, and an empty string is a conversion of
// nothing rather than an error
TEST(HexTest, AcceptsBothDigitCasesAndTheEmptyString) {
  uint8_t payload[8] = {0x00, 0x0f, 0xa5, 0xff, 0x10, 0xde, 0xad, 0xbe};
  hostmem_memory_block block{payload, sizeof(payload)};

  char lower[sizeof(payload) * 2 + 1];
  ASSERT_EQ(grdu_binary_to_hex(lower, &block), HOSTMEM_SUCCESS);

  std::string upper(lower);
  for (char &c : upper) { c = static_cast<char>(toupper(static_cast<unsigned char>(c))); }

  uint8_t from_lower[sizeof(payload)];
  uint8_t from_upper[sizeof(payload)];
  ASSERT_EQ(grdu_binary_from_hex(from_lower, lower), HOSTMEM_SUCCESS);
  ASSERT_EQ(grdu_binary_from_hex(from_upper, upper.c_str()), HOSTMEM_SUCCESS);
  EXPECT_EQ(memcmp(from_lower, payload, sizeof(payload)), 0);
  EXPECT_EQ(memcmp(from_upper, payload, sizeof(payload)), 0);

  uint8_t untouched[4];
  memset(untouched, 0x77, sizeof(untouched));
  EXPECT_EQ(grdu_binary_from_hex(untouched, ""), HOSTMEM_SUCCESS);
  for (unsigned char byte : untouched) { EXPECT_EQ(byte, 0x77); }
}

// promise: anything that is not an even run of hex digits is refused, the output is cleared
// rather than left half converted, and nothing is written past the bytes the string accounts
// for. Separators are the case worth naming: sodium_hex2bin can be told to skip them, this
// cannot, and a caller expecting that should meet a refusal rather than a guess.
TEST(HexTest, RejectsWhatIsNotHexWithoutOverrunning) {
  struct {
    const char *what;
    const char *input;
    hostmem_result expected;
  } const cases[] = {
      {"odd number of digits", "abc", HOSTMEM_ERROR_INVALID_PARAM},
      {"a single digit", "a", HOSTMEM_ERROR_INVALID_PARAM},
      {"not a digit, first position", "zz00", HOSTMEM_ERROR_DECODE_FAILED},
      {"not a digit, low nibble", "azcd", HOSTMEM_ERROR_DECODE_FAILED},
      {"not a digit, last position", "00ffz0", HOSTMEM_ERROR_DECODE_FAILED},
      {"separator between the bytes", "de:ad", HOSTMEM_ERROR_INVALID_PARAM},
      {"separators, even length", "de:ad:be", HOSTMEM_ERROR_DECODE_FAILED},
      {"a space", "de ad", HOSTMEM_ERROR_INVALID_PARAM},
  };

  for (const auto &c : cases) {
    uint8_t guarded[16];
    memset(guarded, 0xCD, sizeof(guarded));

    EXPECT_EQ(grdu_binary_from_hex(guarded, c.input), c.expected) << c.what;

    const size_t decoded_bytes = strlen(c.input) / 2;
    for (size_t i = decoded_bytes; i < sizeof(guarded); ++i) {
      EXPECT_EQ(guarded[i], 0xCD) << c.what << ": wrote past byte " << decoded_bytes;
    }
    if (c.expected == HOSTMEM_ERROR_DECODE_FAILED) {
      for (size_t i = 0; i < decoded_bytes; ++i) {
        EXPECT_EQ(guarded[i], 0) << c.what << ": left a half converted byte at " << i;
      }
    }
  }

  uint8_t out[4];
  EXPECT_EQ(grdu_binary_from_hex(nullptr, "dead"), HOSTMEM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdu_binary_from_hex(out, nullptr), HOSTMEM_ERROR_NULL_POINTER);
}

// promise: the secret pair is a drop in replacement. Whichever a caller picks, the bytes and
// the result code are the same and only the timing differs -- otherwise choosing the safe one
// would be a behaviour change, and nobody would make it in a hurry.
TEST(HexTest, SecretVariantsAnswerExactlyLikeTheFastOnes) {
  for (unsigned value = 0; value < 256; ++value) {
    uint8_t payload[33];
    for (size_t i = 0; i < sizeof(payload); ++i) {
      payload[i] = static_cast<uint8_t>((value + i * 11u) & 0xFFu);
    }
    payload[0] = static_cast<uint8_t>(value);

    // an odd length as well, so the scalar remainder of the fast version is covered
    for (size_t length : {size_t{1}, size_t{16}, size_t{32}, sizeof(payload)}) {
      hostmem_memory_block block{payload, static_cast<uint32_t>(length)};

      char fast[sizeof(payload) * 2 + 1];
      char secret[sizeof(payload) * 2 + 1];
      ASSERT_EQ(grdu_binary_to_hex(fast, &block), grdu_secret_to_hex(secret, &block));
      ASSERT_STREQ(fast, secret) << "value " << value << " length " << length;

      uint8_t from_fast[sizeof(payload)];
      uint8_t from_secret[sizeof(payload)];
      ASSERT_EQ(grdu_binary_from_hex(from_fast, fast), grdu_secret_from_hex(from_secret, secret));
      ASSERT_EQ(memcmp(from_fast, from_secret, length), 0);
      ASSERT_EQ(memcmp(from_fast, payload, length), 0);
    }
  }
}

// promise: the secret pair refuses the same strings for the same reasons, and leaves nothing of
// a half decoded secret behind when it does
TEST(HexTest, SecretVariantsRefuseTheSameStringsAndClearWhatTheyDecoded) {
  struct {
    const char *what;
    const char *input;
    hostmem_result expected;
  } const cases[] = {
      {"odd number of digits", "abc", HOSTMEM_ERROR_INVALID_PARAM},
      {"not a digit, first position", "zz00", HOSTMEM_ERROR_DECODE_FAILED},
      {"not a digit, after two good bytes", "00ffz0", HOSTMEM_ERROR_DECODE_FAILED},
      {"separator between the bytes", "de:ad:be", HOSTMEM_ERROR_DECODE_FAILED},
  };

  for (const auto &c : cases) {
    uint8_t fast[16];
    uint8_t secret[16];
    memset(fast, 0xCD, sizeof(fast));
    memset(secret, 0xCD, sizeof(secret));

    EXPECT_EQ(grdu_binary_from_hex(fast, c.input), c.expected) << c.what;
    EXPECT_EQ(grdu_secret_from_hex(secret, c.input), c.expected) << c.what;

    if (c.expected == HOSTMEM_ERROR_DECODE_FAILED) {
      const size_t covered = strlen(c.input) / 2;
      for (size_t i = 0; i < covered; ++i) {
        EXPECT_EQ(secret[i], 0) << c.what << ": left a decoded byte at " << i;
      }
      EXPECT_EQ(memcmp(fast, secret, covered), 0) << c.what;
    }
    for (size_t i = strlen(c.input) / 2; i < sizeof(secret); ++i) {
      EXPECT_EQ(secret[i], 0xCD) << c.what << ": wrote past what the string covers";
    }
  }

  uint8_t out[4];
  char text[16];
  uint8_t payload[2] = {0xde, 0xad};
  hostmem_memory_block block{payload, sizeof(payload)};
  hostmem_memory_block empty{payload, 0};
  EXPECT_EQ(grdu_secret_from_hex(nullptr, "dead"), HOSTMEM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdu_secret_from_hex(out, nullptr), HOSTMEM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdu_secret_to_hex(nullptr, &block), HOSTMEM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdu_secret_to_hex(text, nullptr), HOSTMEM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdu_secret_to_hex(text, &empty), HOSTMEM_ERROR_INVALID_PARAM);
}

#endif // USE_SODIUM
