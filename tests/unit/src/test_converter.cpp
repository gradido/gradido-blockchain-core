#include "arnm/arena.h"
#include "arnm/memory_block.h"
#include "arnm/mono_timer.h"
#include "gradido_blockchain_core/utils/converter.h"
#include <cstring>
#include <gtest/gtest.h>

#include "arnm/converter.h"
#include "memory_limit.h"
#include <random>
#include <string>
#include <utility>
#include <vector>

#ifdef USE_SODIUM

// what grdu_secret_* is built on, and what the base64 group still uses
#include "sodium.h"

// libsodium answers a destination that is too small by calling sodium_misuse(), which aborts
// the process — so the room has to be checked before the call, not from its return value. The
// NULL check that used to stand there could never fire.
TEST(Base64Test, DestinationTooSmallIsReportedInsteadOfFatal) {
  arnm mem{};
  ASSERT_EQ(arnm_init_arena(&mem, 4096), ARNM_SUCCESS);

  uint8_t payload[32];
  for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<uint8_t>(i);
  arnm_memory_block data{payload, sizeof(payload)};

  const size_t needed = grdu_binary_to_base64_length(sizeof(payload));
  ASSERT_GT(needed, 1u);

  // one byte short: reported, not fatal
  arnm_memory_block tooSmall{};
  ASSERT_EQ(
      arnm_memory_block_alloc(&tooSmall, static_cast<uint32_t>(needed - 1), &mem), ARNM_SUCCESS
  );
  EXPECT_EQ(
      grdu_binary_to_base64_with_known_size(&tooSmall, &data),
      ARNM_ERROR_DESTINATION_BUFFER_TO_SMALL
  );

  // exactly enough: encodes, and the string is terminated where it should be
  arnm_memory_block exact{};
  ASSERT_EQ(arnm_memory_block_alloc(&exact, static_cast<uint32_t>(needed), &mem), ARNM_SUCCESS);
  ASSERT_EQ(grdu_binary_to_base64_with_known_size(&exact, &data), ARNM_SUCCESS);
  EXPECT_EQ(strlen(reinterpret_cast<char *>(exact.data)), needed - 1);

  arnm_release(&mem);
}

// a missing pointer and an empty block are different mistakes and say so
// would be a behaviour change, and nobody would make it in a hurry.
// ---------------------------------------------------------------------------
// arnm's base64 against libsodium's
// ---------------------------------------------------------------------------
//
// The JSON mapping writes body_bytes and the memos as base64 and draws that pair from arnm, so
// that a build without libsodium still has a JSON half. What makes that safe is not that the
// pair round trips against itself -- arnm's own tests hold that -- but that what it writes is
// the same base64 everyone else means by the word. This is where that is checked, because this
// is the binary that has libsodium to check it against.
//
// Every length up to three whole groups past the point where all three padding cases have
// appeared several times, and both directions crossed: what one writes the other reads back.

namespace {

/** libsodium's base64 of @p bytes, ORIGINAL variant -- the standard alphabet with padding. */
std::string SodiumEncode(const std::vector<uint8_t> &bytes) {
  std::string out(sodium_base64_encoded_len(bytes.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
  const char *written = sodium_bin2base64(
      out.data(), out.size(), bytes.data(), bytes.size(), sodium_base64_VARIANT_ORIGINAL
  );
  EXPECT_NE(written, nullptr);
  out.resize(std::strlen(out.c_str()));
  return out;
}

/** arnm's base64 of @p bytes. An empty block is refused rather than encoded, so it answers "". */
std::string ArnmEncode(const std::vector<uint8_t> &bytes) {
  if (bytes.empty()) { return {}; }
  std::string out(ARNM_BASE64_STRING_LENGTH(bytes.size()) + 1u, '\0');
  const arnm_memory_block block{const_cast<uint8_t *>(bytes.data()), (uint32_t)bytes.size()};
  EXPECT_EQ(arnm_binary_to_base64(out.data(), &block), ARNM_SUCCESS);
  out.resize(std::strlen(out.c_str()));
  return out;
}

std::vector<uint8_t> PayloadOfLength(size_t length) {
  std::vector<uint8_t> bytes(length);
  // every byte value turns up, so a table or a mask that is wrong anywhere is wrong here
  for (size_t i = 0; i < length; ++i) { bytes[i] = (uint8_t)((i * 61u + length * 7u) & 0xFFu); }
  return bytes;
}

} // namespace

TEST(Base64Test, ArnmWritesTheSameTextLibsodiumDoes) {
  for (size_t length = 1; length <= 200; ++length) {
    const std::vector<uint8_t> bytes = PayloadOfLength(length);
    EXPECT_EQ(ArnmEncode(bytes), SodiumEncode(bytes)) << "length " << length;
  }
}

TEST(Base64Test, LibsodiumReadsBackWhatArnmWrote) {
  for (size_t length = 1; length <= 200; ++length) {
    const std::vector<uint8_t> bytes = PayloadOfLength(length);
    const std::string text = ArnmEncode(bytes);

    std::vector<uint8_t> back(length + 4u);
    size_t written = 0;
    ASSERT_EQ(
        sodium_base642bin(
            back.data(), back.size(), text.c_str(), text.size(), nullptr, &written, nullptr,
            sodium_base64_VARIANT_ORIGINAL
        ),
        0
    ) << "length "
      << length;
    ASSERT_EQ(written, length);
    EXPECT_EQ(0, std::memcmp(back.data(), bytes.data(), length)) << "length " << length;
  }
}

TEST(Base64Test, ArnmReadsBackWhatLibsodiumWrote) {
  for (size_t length = 1; length <= 200; ++length) {
    const std::vector<uint8_t> bytes = PayloadOfLength(length);
    const std::string text = SodiumEncode(bytes);

    std::vector<uint8_t> back(ARNM_BASE64_BINARY_SIZE(text.size()) + 4u);
    uint32_t written = 0;
    ASSERT_EQ(arnm_binary_from_base64(back.data(), &written, text.c_str()), ARNM_SUCCESS)
        << "length " << length;
    ASSERT_EQ(written, length);
    EXPECT_EQ(0, std::memcmp(back.data(), bytes.data(), length)) << "length " << length;
  }
}

TEST(Base64Test, TheTwoAgreeOnTheVectorsRfc4648Prints) {
  // pinned against both, so a change to either is caught by the same rows
  const std::pair<const char *, const char *> vectors[] = {
      {"f", "Zg=="},        {"fo", "Zm8="},        {"foo", "Zm9v"},
      {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"},
  };
  for (const auto &[input, expected] : vectors) {
    const std::vector<uint8_t> bytes(
        reinterpret_cast<const uint8_t *>(input),
        reinterpret_cast<const uint8_t *>(input) + std::strlen(input)
    );
    EXPECT_EQ(ArnmEncode(bytes), expected) << input;
    EXPECT_EQ(SodiumEncode(bytes), expected) << input;
  }
}

TEST(Base64Test, WhereTheTwoDifferIsStrictnessAndNotTheAlphabet) {
  // libsodium takes an "ignore" set and skips those characters; arnm takes none and refuses
  // anything outside the alphabet. Both are defensible and only one of them is arnm's, so the
  // difference is pinned rather than left for someone to discover by swapping the pair out.
  const char wrapped[] = "Zm9v\nYmFy";

  uint8_t sodium_out[8] = {0};
  size_t sodium_written = 0;
  EXPECT_EQ(
      sodium_base642bin(
          sodium_out, sizeof(sodium_out), wrapped, std::strlen(wrapped), "\n", &sodium_written,
          nullptr, sodium_base64_VARIANT_ORIGINAL
      ),
      0
  ) << "libsodium skips what its ignore set names";
  EXPECT_EQ(sodium_written, 6u);

  uint8_t arnm_out[8] = {0};
  uint32_t arnm_written = 0;
  // nine characters once the newline is counted, which is not a whole number of groups
  EXPECT_EQ(arnm_binary_from_base64(arnm_out, &arnm_written, wrapped), ARNM_ERROR_INVALID_PARAM)
      << "arnm has no ignore set and does not grow one by accident";
}

TEST(HexTest, SecretVariantsAnswerExactlyLikeTheFastOnes) {
  for (unsigned value = 0; value < 256; ++value) {
    uint8_t payload[33];
    for (size_t i = 0; i < sizeof(payload); ++i) {
      payload[i] = static_cast<uint8_t>((value + i * 11u) & 0xFFu);
    }
    payload[0] = static_cast<uint8_t>(value);

    // an odd length as well, so the scalar remainder of the fast version is covered
    for (size_t length : {size_t{1}, size_t{16}, size_t{32}, sizeof(payload)}) {
      arnm_memory_block block{payload, static_cast<uint32_t>(length)};

      char fast[sizeof(payload) * 2 + 1];
      char secret[sizeof(payload) * 2 + 1];
      ASSERT_EQ(arnm_binary_to_hex(fast, &block), grdu_secret_to_hex(secret, &block));
      ASSERT_STREQ(fast, secret) << "value " << value << " length " << length;

      uint8_t from_fast[sizeof(payload)];
      uint8_t from_secret[sizeof(payload)];
      ASSERT_EQ(arnm_binary_from_hex(from_fast, fast), grdu_secret_from_hex(from_secret, secret));
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
    arnm_result expected;
  } const cases[] = {
      {"odd number of digits", "abc", ARNM_ERROR_INVALID_PARAM},
      {"not a digit, first position", "zz00", ARNM_ERROR_DECODE_FAILED},
      {"not a digit, after two good bytes", "00ffz0", ARNM_ERROR_DECODE_FAILED},
      {"separator between the bytes", "de:ad:be", ARNM_ERROR_DECODE_FAILED},
  };

  for (const auto &c : cases) {
    uint8_t fast[16];
    uint8_t secret[16];
    memset(fast, 0xCD, sizeof(fast));
    memset(secret, 0xCD, sizeof(secret));

    EXPECT_EQ(arnm_binary_from_hex(fast, c.input), c.expected) << c.what;
    EXPECT_EQ(grdu_secret_from_hex(secret, c.input), c.expected) << c.what;

    const size_t covered = strlen(c.input) / 2;
    if (c.expected == ARNM_ERROR_DECODE_FAILED) {
      for (size_t i = 0; i < covered; ++i) {
        EXPECT_EQ(secret[i], 0) << c.what << ": left a decoded byte at " << i;
      }
      EXPECT_EQ(memcmp(fast, secret, covered), 0) << c.what;
    } else {
      // A parameter error is refused before anything is written, so what the caller had in the
      // buffer is still there. Clearing it would mean erasing bytes this call never produced --
      // and the range to clear is not knowable from a signature that derives its length from the
      // string. Wiping a buffer that has served its purpose stays with whoever owns it.
      for (size_t i = 0; i < covered; ++i) {
        EXPECT_EQ(secret[i], 0xCD) << c.what << ": touched a buffer it had refused, at " << i;
        EXPECT_EQ(fast[i], 0xCD) << c.what << ": touched a buffer it had refused, at " << i;
      }
    }
    for (size_t i = strlen(c.input) / 2; i < sizeof(secret); ++i) {
      EXPECT_EQ(secret[i], 0xCD) << c.what << ": wrote past what the string covers";
    }
  }

  uint8_t out[4];
  char text[16];
  uint8_t payload[2] = {0xde, 0xad};
  arnm_memory_block block{payload, sizeof(payload)};
  arnm_memory_block empty{payload, 0};
  EXPECT_EQ(grdu_secret_from_hex(nullptr, "dead"), ARNM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdu_secret_from_hex(out, nullptr), ARNM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdu_secret_to_hex(nullptr, &block), ARNM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdu_secret_to_hex(text, nullptr), ARNM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdu_secret_to_hex(text, &empty), ARNM_ERROR_INVALID_PARAM);
}

#endif // USE_SODIUM
