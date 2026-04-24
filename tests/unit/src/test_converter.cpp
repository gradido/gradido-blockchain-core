#include <gtest/gtest.h>
#include "gradido_blockchain_core/utils/converter.h"
#include "gradido_blockchain_core/utils/mono_timer.h"

#include <random>

TEST(Converter, grdu_uint64_to_string)
{
    char buffer[20];
    auto expectedSize = grdu_uint64_to_string(buffer, sizeof(buffer), 123456789);
    EXPECT_EQ(expectedSize, 9);
    EXPECT_STREQ(buffer, "123456789");
}

TEST(Converter, grdu_uint64_to_string_full)
{
    char buffer[20];
    auto expectedSize = grdu_uint64_to_string(buffer, sizeof(buffer), 1234567890123456789);
    EXPECT_EQ(expectedSize, 19);
    EXPECT_STREQ(buffer, "1234567890123456789");
}

TEST(Converter, grdu_uint64_to_string_empty)
{
    char buffer[20];
    auto expectedSize = grdu_uint64_to_string(buffer, sizeof(buffer), 0);
    EXPECT_EQ(expectedSize, 1);
    EXPECT_STREQ(buffer, "0");
}

TEST(Converter, grdu_uint64_to_string_too_small_buffer)
{
    char buffer[1];
    auto expectedSize = grdu_uint64_to_string(buffer, sizeof(buffer), 123456789);
    EXPECT_EQ(expectedSize, 9);
    EXPECT_STREQ(buffer, "");
}

size_t grdu_uint64_to_string_size_old(uint64_t value)
{
    static uint64_t powers[] = {
        10, 100, 1000, 10000, 100000,
        1000000, 10000000, 100000000,
        1000000000, 10000000000, 100000000000,
        1000000000000, 10000000000000, 100000000000000,
        1000000000000000, 10000000000000000, 100000000000000000,
        1000000000000000000, 10000000000000000000u
    };
    int i = 0;
    while (value >= powers[i] && i < 18) {
        ++i;
    }
    return i + 1;
}

TEST(Converter, grdu_uint64_to_string_size_validation)
{
    auto ref = grdu_uint64_to_string_size_old;
    auto opt = grdu_uint64_to_string_size;

    // --- 1. Explicit Edge Cases ---
    uint64_t cases[] = {
        0ULL, 1ULL, 9ULL, 10ULL,
        99ULL, 100ULL, 999ULL, 1000ULL,
        UINT64_MAX
    };

    for (uint64_t v : cases) {
        ASSERT_EQ(ref(v), opt(v)) << "Edge case failed: " << v;
    }

    // --- 2. Boundaries around powers of 10 ---
    uint64_t p = 1;
    for (int d = 1; d <= 19; d++)
    {
        uint64_t low  = p;
        uint64_t high = p * 10 - 1;

        for (int i = -2; i <= 2; i++) {
            if (low + i > 0) {
                ASSERT_EQ(ref(low + i), opt(low + i)) << "Low boundary: " << (low + i);
            }

            ASSERT_EQ(ref(high + i), opt(high + i)) << "High boundary: " << (high + i);
        }

        p *= 10;
    }

    // --- 3. Bit-structure tests (logarithmic distribution) ---
    for (uint64_t v = 1; v != 0; v <<= 1) {
        ASSERT_EQ(ref(v), opt(v)) << "Power of 2: " << v;

        if (v > 0) ASSERT_EQ(ref(v - 1), opt(v - 1));
        if (v < UINT64_MAX) ASSERT_EQ(ref(v + 1), opt(v + 1));
    }

    // --- 4. Random values ---
    std::mt19937_64 rng(123456);

    for (size_t i = 0; i < 5'000'000; i++) {
        uint64_t v = rng();
        ASSERT_EQ(ref(v), opt(v));
    }
}