#include "gradido_blockchain_core/data/timestamp.h"
#include "memory_limit.h"
#include <climits>
#include <cstdint>
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

// nanos outside [0, 999999999] has no 9 digit representation. It used to drive the zero
// padding negative, and memset took that as a length near SIZE_MAX — a stack overflow
// reachable from a decoded transaction, since no wire type bounds the field.
TEST(TimestampToString, RejectsNanosOutOfRange) {
  char buffer[64];
  for (int32_t bad : {-1, -999999999, -1000000000, INT32_MIN, 1000000000, INT32_MAX}) {
    grdd_timestamp ts = {.seconds = 1, .nanos = bad};
    EXPECT_EQ(grdd_timestamp_to_string(buffer, sizeof(buffer), &ts), 0u) << "nanos " << bad;
    EXPECT_EQ(grdd_timestamp_calculate_string_size(&ts), 0u) << "nanos " << bad;
  }

  // the edges of the valid range still work
  for (int32_t good : {0, 1, 999999999}) {
    grdd_timestamp ts = {.seconds = 1, .nanos = good};
    EXPECT_EQ(grdd_timestamp_to_string(buffer, sizeof(buffer), &ts), 11u) << "nanos " << good;
    EXPECT_EQ(grdd_timestamp_calculate_string_size(&ts), 11u) << "nanos " << good;
  }
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

// ---------------------------------------------------------------------------
// arithmetic: normalized results and no overflow
// ---------------------------------------------------------------------------

// A timestamp is seconds + nanos/1e9, so whole seconds may move between the fields. Both
// operations put the result back into nanos ∈ [0, 1e9) — the range grdd_timestamp_to_string
// can print, and the one nothing in the wire types enforces on the way in.
TEST(TimestampArithmetic, NormalizesOutOfRangeNanos) {
  char buffer[64];

  // an operand carrying more than a second's worth of nanos
  grdd_timestamp a = {.seconds = 5, .nanos = 1500000000};
  grdd_timestamp zero = {.seconds = 0, .nanos = 0};
  auto r = grdd_timestamp_plus(&a, &zero);
  EXPECT_EQ(r.seconds, 6);
  EXPECT_EQ(r.nanos, 500000000);
  EXPECT_EQ(grdd_timestamp_to_string(buffer, sizeof(buffer), &r), 11u); // 1 + '.' + 9
  EXPECT_STREQ(buffer, "6.500000000");

  // and one carrying a negative amount
  grdd_timestamp b = {.seconds = 5, .nanos = -1500000000};
  r = grdd_timestamp_plus(&b, &zero);
  EXPECT_EQ(r.seconds, 3);
  EXPECT_EQ(r.nanos, 500000000);

  // a difference that borrows across the second boundary stays normalized
  grdd_timestamp t1 = {.seconds = 0, .nanos = 0};
  grdd_timestamp t2 = {.seconds = 0, .nanos = 1};
  r = grdd_timestamp_minus(&t1, &t2);
  EXPECT_EQ(r.seconds, -1);
  EXPECT_EQ(r.nanos, 999999999);
  EXPECT_EQ(grdd_timestamp_to_string(buffer, sizeof(buffer), &r), 12u);
  EXPECT_STREQ(buffer, "-1.999999999");
}

TEST(TimestampArithmetic, ResultIsAlwaysPrintable) {
  // whatever comes in, the result can be fed straight to the string functions
  const int32_t nanos[] = {INT32_MIN, -1000000001, -1, 0, 1, 999999999, 1000000000, INT32_MAX};
  const int64_t seconds[] = {INT64_MIN, -1, 0, 1, INT64_MAX};
  char buffer[64];
  for (int64_t s1 : seconds) {
    for (int32_t n1 : nanos) {
      for (int64_t s2 : seconds) {
        for (int32_t n2 : nanos) {
          grdd_timestamp a = {.seconds = s1, .nanos = n1};
          grdd_timestamp b = {.seconds = s2, .nanos = n2};
          for (auto r : {grdd_timestamp_plus(&a, &b), grdd_timestamp_minus(&a, &b)}) {
            ASSERT_GE(r.nanos, 0);
            ASSERT_LT(r.nanos, 1000000000);
            ASSERT_NE(grdd_timestamp_to_string(buffer, sizeof(buffer), &r), 0u);
          }
        }
      }
    }
  }
}

TEST(TimestampArithmetic, SaturatesInsteadOfOverflowing) {
  grdd_timestamp max = {.seconds = INT64_MAX, .nanos = 0};
  grdd_timestamp min = {.seconds = INT64_MIN, .nanos = 0};
  grdd_timestamp one = {.seconds = 1, .nanos = 0};

  EXPECT_EQ(grdd_timestamp_plus(&max, &one).seconds, INT64_MAX);
  EXPECT_EQ(grdd_timestamp_plus(&max, &max).seconds, INT64_MAX);
  EXPECT_EQ(grdd_timestamp_minus(&min, &one).seconds, INT64_MIN);
  EXPECT_EQ(grdd_timestamp_minus(&min, &max).seconds, INT64_MIN);
  // INT64_MIN - INT64_MIN is representable and must not be pinned
  EXPECT_EQ(grdd_timestamp_minus(&min, &min).seconds, 0);
  EXPECT_EQ(grdd_timestamp_minus(&max, &max).seconds, 0);
  // the borrow at the very bottom has nowhere to go either
  grdd_timestamp min_borrow = {.seconds = INT64_MIN, .nanos = 0};
  grdd_timestamp tick = {.seconds = 0, .nanos = 1};
  EXPECT_EQ(grdd_timestamp_minus(&min_borrow, &tick).seconds, INT64_MIN);
}

TEST(TimestampArithmetic, ToleratesNullOperands) {
  grdd_timestamp t = {.seconds = 7, .nanos = 8};
  for (auto r :
       {grdd_timestamp_plus(nullptr, &t), grdd_timestamp_plus(&t, nullptr),
        grdd_timestamp_minus(nullptr, &t), grdd_timestamp_minus(&t, nullptr),
        grdd_timestamp_plus(nullptr, nullptr)}) {
    EXPECT_EQ(r.seconds, 0);
    EXPECT_EQ(r.nanos, 0);
  }
}
