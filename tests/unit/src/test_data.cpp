#include "gradido_blockchain_core/data/timestamp.h"
#include "memory_limit.h"
#include <climits>
#include <gtest/gtest.h>

static size_t expected_length(const grdd_timestamp *ts) {
  char temp[64];
  snprintf(temp, sizeof(temp), "%lld.%.9d", (long long)ts->seconds, ts->nanos);
  return strlen(temp);
}

TEST(TimestampToString, Basic) {
  grdd_timestamp ts = {.seconds = 1781598032, .nanos = 32260316};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 20);
  EXPECT_STREQ(buffer, "1781598032.032260316");
}

TEST(TimestampToString, LeadingZeros) {
  grdd_timestamp ts = {.seconds = 123456789, .nanos = 1234};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 19); // 9 Sekunden + 1 Punkt + 9 Nanos = 19
  EXPECT_STREQ(buffer, "123456789.000001234");
}

TEST(TimestampToString, AllZeros) {
  grdd_timestamp ts = {.seconds = 0, .nanos = 0};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 11); // "0.000000000" = 11 Zeichen
  EXPECT_STREQ(buffer, "0.000000000");
}

TEST(TimestampToString, AllNines) {
  grdd_timestamp ts = {.seconds = 123456789, .nanos = 999999999};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 19);
  EXPECT_STREQ(buffer, "123456789.999999999");
}

TEST(TimestampToString, NegativeSeconds) {
  grdd_timestamp ts = {.seconds = -123, .nanos = 456789};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 14); // "-123.000456789" = 16 Zeichen
  EXPECT_STREQ(buffer, "-123.000456789");
}

TEST(TimestampToString, NegativeSecondsWithNanos) {
  grdd_timestamp ts = {.seconds = -1, .nanos = 500000000};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 12); // "-1.500000000"
  EXPECT_STREQ(buffer, "-1.500000000");
}

TEST(TimestampToString, MaxSeconds) {
  grdd_timestamp ts = {.seconds = INT64_MAX, .nanos = 0};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  // Erwartete Länge: 19 (max. 19 Stellen für INT64_MAX) + 1 + 9 = 29
  ASSERT_EQ(written, 29);
  std::string expected = std::to_string(INT64_MAX) + ".000000000";
  EXPECT_STREQ(buffer, expected.c_str());
}

TEST(TimestampToString, MinSeconds) {
  grdd_timestamp ts = {.seconds = INT64_MIN, .nanos = 0};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  // INT64_MIN hat 20 Zeichen (weil negativ)
  ASSERT_EQ(written, 30); // 20 + 1 + 9
  std::string expected = std::to_string(INT64_MIN) + ".000000000";
  EXPECT_STREQ(buffer, expected.c_str());
}

TEST(TimestampToString, BufferTooSmall) {
  grdd_timestamp ts = {.seconds = 123456789, .nanos = 123456789};
  char buffer[10];
  size_t required = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  // Erwartet: 9 + 1 + 9 = 19
  ASSERT_EQ(required, 19);
  // Buffer sollte unverändert sein (oder wir prüfen, dass nichts drüber geschrieben wurde)
  // Da wir die Größe nicht prüfen können, testen wir, dass der Puffer nicht überschrieben wurde
  // Wir füllen mit 'x' und prüfen, ob die ersten bytes noch 'x' sind
  memset(buffer, 'x', sizeof(buffer));
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 19);
  // Der Puffer sollte immer noch mit 'x' gefüllt sein, weil nichts geschrieben wurde
  for (size_t i = 0; i < sizeof(buffer); ++i) { ASSERT_EQ(buffer[i], 'x'); }
}

TEST(TimestampToString, BufferExactlyRequired) {
  grdd_timestamp ts = {.seconds = 123456789, .nanos = 0};
  size_t required = 10; // "123456789.000000000" = 19? Warte: 123456789 = 9 Ziffern, Punkt + 9 = 19.
  char buffer[19];
  memset(buffer, 0, sizeof(buffer));
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 19);
  EXPECT_STREQ(buffer, "123456789.000000000");
}

TEST(TimestampCalculateStringSize, Basic) {
  grdd_timestamp ts = {.seconds = 123456789, .nanos = 0};
  size_t size = grdd_timestamp_calculate_string_size(&ts);
  ASSERT_EQ(size, 19); // 9 + 1 + 9
}

TEST(TimestampCalculateStringSize, Negative) {
  grdd_timestamp ts = {.seconds = -123456789, .nanos = 0};
  size_t size = grdd_timestamp_calculate_string_size(&ts);
  ASSERT_EQ(size, 20); // 10 (wegen Minus) + 1 + 9
}

TEST(TimestampCalculateStringSize, Null) {
  size_t size = grdd_timestamp_calculate_string_size(nullptr);
  ASSERT_EQ(size, 0);
}

TEST(TimestampMinus, Basic) {
  grdd_timestamp t1 = {.seconds = 10, .nanos = 500000000};
  grdd_timestamp t2 = {.seconds = 3, .nanos = 200000000};
  auto result = grdd_timestamp_minus(&t1, &t2);
  ASSERT_EQ(result.seconds, 7);
  ASSERT_EQ(result.nanos, 300000000);
}

TEST(TimestampMinus, Borrow) {
  grdd_timestamp t1 = {.seconds = 5, .nanos = 100000000};
  grdd_timestamp t2 = {.seconds = 2, .nanos = 500000000};
  auto result = grdd_timestamp_minus(&t1, &t2);
  ASSERT_EQ(result.seconds, 2);
  ASSERT_EQ(result.nanos, 600000000);
}

TEST(TimestampPlus, Basic) {
  grdd_timestamp t1 = {.seconds = 5, .nanos = 100000000};
  grdd_timestamp t2 = {.seconds = 2, .nanos = 200000000};
  auto result = grdd_timestamp_plus(&t1, &t2);
  ASSERT_EQ(result.seconds, 7);
  ASSERT_EQ(result.nanos, 300000000);
}

TEST(TimestampPlus, Carry) {
  grdd_timestamp t1 = {.seconds = 5, .nanos = 800000000};
  grdd_timestamp t2 = {.seconds = 2, .nanos = 500000000};
  auto result = grdd_timestamp_plus(&t1, &t2);
  ASSERT_EQ(result.seconds, 8);
  ASSERT_EQ(result.nanos, 300000000);
}
