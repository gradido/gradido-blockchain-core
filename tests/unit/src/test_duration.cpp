#include <gtest/gtest.h>
#include "gradido_blockchain_core/utils/duration.h"

TEST(Duration, Ns)
{
    char buffer[10];
    grdu_duration_string(buffer, sizeof(buffer), (grdu_duration)500, 0);
    EXPECT_STREQ(buffer, "500 ns");
}

TEST(Duration, Microseconds)
{
    char buffer[10];
    grdu_duration_string(buffer, sizeof(buffer), (grdu_duration)1234, 3);
    EXPECT_STREQ(buffer, "1.234 us");
}

TEST(Duration, Milliseconds)
{
    char buffer[10];
    grdu_duration_string(buffer, sizeof(buffer), (grdu_duration)1500000LL, 3);
    EXPECT_STREQ(buffer, "1.500 ms");
}

TEST(Duration, Seconds)
{
    char buffer[10];
    grdu_duration_string(buffer, sizeof(buffer), (grdu_duration)1500000000LL, 3);
    EXPECT_STREQ(buffer, "1.500 s");
}

TEST(Duration, Minutes)
{
    char buffer[10];
    grdu_duration ns = 90LL * 1000000000LL; // 90 seconds -> 1.5 minutes
    grdu_duration_string(buffer, sizeof(buffer), ns, 3);
    EXPECT_STREQ(buffer, "1.500 m");
}


TEST(Duration, Hours)
{
    char buffer[10];
    grdu_duration ns = 3ULL * 3600ULL * 1000000000ULL; // 3 hours
    grdu_duration_string(buffer, sizeof(buffer), ns, 3);
    EXPECT_STREQ(buffer, "3.000 h");
}

TEST(Duration, Days)
{
    char buffer[10];
    grdu_duration ns = 36ULL * 3600ULL * 1000000000ULL; // 36 hours -> 1.5 days
    grdu_duration_string(buffer, sizeof(buffer), ns, 1);
    EXPECT_STREQ(buffer, "1.5 d");
}

TEST(Duration, DaysMoreDecimal)
{
    char buffer[25];
    grdu_duration ns = 36ULL * 3600ULL * 1500700030ULL; // 36 hours -> 1.5 days
    grdu_duration_string(buffer, sizeof(buffer), ns, 10);
    EXPECT_STREQ(buffer, "2.2510500449 d");
}
