#include <gtest/gtest.h>
#include "gradido_blockchain_core/utils/converter.h"

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
