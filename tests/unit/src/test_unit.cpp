#include <gtest/gtest.h>

#include "gradido_blockchain_core/data/unit.h"
#include "gradido_blockchain_core/utils/mono_timer.h"
#include "../terminal_colors.h"

#include <iomanip>
#include <math.h>
#include <random>

TEST(TestUnit, fromDouble)
{
  double gddD = 0.0;
  EXPECT_EQ(grdd_unit_from_decimal(gddD), 0);
}

TEST(TestUnit, fromToString)
{
  const char* testString = "1029.1021";
  grdd_unit value;
  grdd_unit_from_string(&value, testString);
  EXPECT_EQ(value, 10291021);
  char buffer[45];
  grdd_unit_to_string(buffer, 45, value, 4);
  EXPECT_STREQ(testString, buffer);
  grdd_unit_to_string(buffer, 45, value, 8);
  EXPECT_STREQ(testString, buffer);
  grdd_unit_to_string(buffer, 45, value, 2);
  EXPECT_STREQ("1029.10", buffer);
}


TEST(GradidoUnitTest, toString_AllCases)
{
  constexpr int bufferSize = 32;
  char buffer[bufferSize];
  // positive values
  grdd_unit v = grdd_unit_from_decimal(0.0);
  grdd_unit_to_string(buffer, bufferSize, v, 0);
  EXPECT_STREQ("0", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 1);
  EXPECT_STREQ("0.0", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 2);
  EXPECT_STREQ("0.00", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 3);
  EXPECT_STREQ("0.000", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("0.0000", buffer);

  v = grdd_unit_from_decimal(0.0001);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("0.0001", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 3);
  EXPECT_STREQ("0.000", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 2);
  EXPECT_STREQ("0.00", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 1);
  EXPECT_STREQ("0.0", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 0);
  EXPECT_STREQ("0", buffer);

  v = grdd_unit_from_decimal(0.001);
  grdd_unit_to_string(buffer, bufferSize, v, 3);
  EXPECT_STREQ("0.001", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("0.0010", buffer);

  v = grdd_unit_from_decimal(1.0);
  grdd_unit_to_string(buffer, bufferSize, v, 0);
  EXPECT_STREQ("1", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 1);
  EXPECT_STREQ("1.0", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 2);
  EXPECT_STREQ("1.00", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 3);
  EXPECT_STREQ("1.000", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("1.0000", buffer);

  v = grdd_unit_from_decimal(1234.5678);
  grdd_unit_to_string(buffer, bufferSize, v, 0);
  EXPECT_STREQ("1235", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 1);
  EXPECT_STREQ("1234.6", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 2);
  EXPECT_STREQ("1234.57", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 3);
  EXPECT_STREQ("1234.568", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("1234.5678", buffer);

  // negative values
  v = grdd_unit_from_decimal(-0.0);
  grdd_unit_to_string(buffer, bufferSize, v, 0);
  EXPECT_STREQ("0", buffer);

  v = grdd_unit_from_decimal(-0.0001);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("-0.0001", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 3);
  EXPECT_STREQ("0.000", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 2);
  EXPECT_STREQ("0.00", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 1);
  EXPECT_STREQ("0.0", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 0);
  EXPECT_STREQ("0", buffer);

  v = grdd_unit_from_decimal(-1.0);
  grdd_unit_to_string(buffer, bufferSize, v, 0);
  EXPECT_STREQ("-1", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 1);
  EXPECT_STREQ("-1.0", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 2);
  EXPECT_STREQ("-1.00", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 3);
  EXPECT_STREQ("-1.000", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("-1.0000", buffer);

  v = grdd_unit_from_decimal(-1234.5678);
  grdd_unit_to_string(buffer, bufferSize, v, 0);
  EXPECT_STREQ("-1235", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 1);
  EXPECT_STREQ("-1234.6", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 2);
  EXPECT_STREQ("-1234.57", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 3);
  EXPECT_STREQ("-1234.568", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("-1234.5678", buffer);

  // precision overflow, auto capped to 4
  v = grdd_unit_from_decimal(1.2345678);
  grdd_unit_to_string(buffer, bufferSize, v, 5);
  EXPECT_STREQ("1.2346", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 10);
  EXPECT_STREQ("1.2346", buffer);

  // check if round work as expected
  v = grdd_unit_from_decimal(0.00005);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("0.0001", buffer);
  v = grdd_unit_from_decimal(0.00004);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("0.0000", buffer);

  v = 3000;
  grdd_unit_to_string(buffer, bufferSize, v, 1);
  EXPECT_STREQ("0.3", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 2);
  EXPECT_STREQ("0.30", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 3);
  EXPECT_STREQ("0.300", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("0.3000", buffer);

  v = 300;
  grdd_unit_to_string(buffer, bufferSize, v, 1);
  EXPECT_STREQ("0.0", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 2);
  EXPECT_STREQ("0.03", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 3);
  EXPECT_STREQ("0.030", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("0.0300", buffer);

  v = 3070;
  grdd_unit_to_string(buffer, bufferSize, v, 1);
  EXPECT_STREQ("0.3", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 2);
  EXPECT_STREQ("0.31", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 3);
  EXPECT_STREQ("0.307", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("0.3070", buffer);

  v = 9223372036854775807;
  // Calls fail if biggest value for int64: 9223372036854775807 is exceeded
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ(buffer, "922337203685477.5807");
  // this would round up to: 9223372036854775810 which exceeds int64
  EXPECT_EQ(grdd_unit_to_string(buffer, bufferSize, v, 3), -2);
  EXPECT_GT(grdd_unit_to_string(buffer, bufferSize, v, 2), 0);
  EXPECT_STREQ(buffer, "922337203685477.58");
  // this would round up to: 9223372036854776000 which exceeds int64
  EXPECT_EQ(grdd_unit_to_string(buffer, bufferSize, v, 1), -2);
  // this would round up to: 9223372036854780000 which exceeds int64
  EXPECT_EQ(grdd_unit_to_string(buffer, bufferSize, v, 0), -2);

  v = -9223372036854775807;
  // Calls fail if biggest value for int64: -9223372036854775807 is exceeded
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ(buffer, "-922337203685477.5807");
  // this would round up to: -9223372036854775810 which exceeds int64
  EXPECT_EQ(grdd_unit_to_string(buffer, bufferSize, v, 3), -2);
  EXPECT_GT(grdd_unit_to_string(buffer, bufferSize, v, 2), 0);
  EXPECT_STREQ(buffer, "-922337203685477.58");
  // this would round up to: -9223372036854776000 which exceeds int64
  EXPECT_EQ(grdd_unit_to_string(buffer, bufferSize, v, 1), -2);
  // this would round up to: -9223372036854780000 which exceeds int64
  EXPECT_EQ(grdd_unit_to_string(buffer, bufferSize, v, 0), -2);

  // bigger number
  v = grdd_unit_from_decimal(987654321.1234);
  grdd_unit_to_string(buffer, bufferSize, v, 0);
  EXPECT_STREQ("987654321", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 1);
  EXPECT_STREQ("987654321.1", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 2);
  EXPECT_STREQ("987654321.12", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 3);
  EXPECT_STREQ("987654321.123", buffer);
  grdd_unit_to_string(buffer, bufferSize, v, 4);
  EXPECT_STREQ("987654321.1234", buffer);

  // number outside of double range
}

TEST(GradidoUnitTest, roundToPrecision_EdgeCases)
{
  struct TestCase {
    int64_t raw; // GradidoCent (4 decimal places)
    int precision;
    int64_t expected;
  };

  std::vector<TestCase> cases = {

    // --- simple cases ---
    {1234500, 2, 1234500},   // 123.4500 -> 123.45
    {1234567, 2, 1234600},   // 123.4567 -> 123.46
    {1234549, 2, 1234500},   // 123.4549 -> 123.45

    // --- critical .5 boundary ---
    {1000500, 1, 1001000},   // 100.0500 -> 100.050 -> round up
    {1000499, 1, 1000000},   // 100.0499 -> down

    // --- precision 0 ---
    {1234999, 0, 1230000},   // 123.4999 -> 123
    {1235000, 0, 1240000},   // 123.5000 -> 124

    // --- carry in integer ---
    {9999500, 0, 10000000},  // 999.9500 -> 1000
    {19999500, 1, 20000000}, // 1999.9500 -> 2000.0

    // --- negative values ---
    {-1234500, 2, -1234500}, // -123.4500 -> -123.45
    {-1234567, 2, -1234600}, // -123.4567 -> -123.46
    {-1234549, 2, -1234500}, // -123.4549 -> -123.45

    // --- critical negative .5 ---
    {-1235000, 0, -1240000}, // -123.5000 -> -124 (!! important !!)
    {-1234999, 0, -1230000}, // -123.4999 -> -123

    // --- small values ---
    {5, 4, 5},               // 0.0005 -> remains
    {5, 3, 10},               // 0.0005 -> 0.001
    {4, 3, 0},               // 0.0004 -> 0.000

    // --- null ---
    {0, 0, 0},
    {0, 4, 0},

    // --- extreme values ---
    {INT64_MAX / 10, 4, INT64_MAX / 10}, // only stability
    {INT64_MIN / 10, 4, INT64_MIN / 10}
  };

  for (const auto& t : cases) {
    auto v = t.raw;
    grdd_unit rounded;
    EXPECT_TRUE(grdd_unit_round_to_precision(&rounded, v, t.precision));

    EXPECT_EQ(rounded, t.expected)
      << "raw=" << t.raw
      << " precision=" << t.precision;
  }
}


TEST(GradidoUnitTest, roundToPrecision_BoundarySweep)
{
  for (int precision = 0; precision <= 4; ++precision)
  {
    int64_t factor = pow(10.0, 4 - precision);

    for (int64_t base = -10; base <= 10; ++base)
    {
      int64_t center = base * factor;

      // test around the 5. border
      for (int offset = -10; offset <= 10; ++offset)
      {
        int64_t raw = center + offset;

        auto v = raw;
        grdd_unit r;
        EXPECT_TRUE(grdd_unit_round_to_precision(&r, v, precision));

        int64_t expected =
          (raw >= 0)
          ? (raw + factor / 2) / factor * factor
          : (raw - factor / 2) / factor * factor;

        ASSERT_EQ(r, expected)
          << "raw=" << raw
          << " precision=" << precision;
      }
    }
  }
}


TEST(GradidoUnitTest, toString_Randomized)
{
  std::mt19937_64 rng(42); // deterministisch
  std::uniform_real_distribution<double> dist(-1e9, 1e9); // innerhalb double-Bereich
  int countDiffBetweenDoubleInteger = 0;
  constexpr int bufferSize = 24;
  char buffer[bufferSize];
  
  for (int i = 0; i < 10'000; ++i) 
  {
    double value = dist(rng);
    grdd_unit v = grdd_unit_from_decimal(value);
    
    for (int precision = 0; precision <= 4; ++precision) 
    {
      EXPECT_GT(grdd_unit_to_string(buffer, bufferSize, v, precision), 0);

      ASSERT_FALSE(precision < 0 || precision > 4) << "expect precision in the range [0;4]";
      std::stringstream ss;
      ss << std::fixed << std::setprecision(precision);

      double rounded = grdd_unit_to_decimal(v);

      if (precision < 4) 
      {
        double factor = pow(10.0, precision);
        rounded = round(grdd_unit_to_decimal(v) * factor) / factor;
      }
      
      ss << rounded;
      std::string refStr(ss.str());

      if (refStr.compare(buffer) != 0) {
        ++countDiffBetweenDoubleInteger;
      }
      //EXPECT_EQ(fastStr, refStr) << "value=" << value << " precision=" << precision << " i=" << i;
      // ASSERT_STREQ(buffer, rBuffer) << "value=" << value << " precision=" << precision << " i=" << i;
    }
  }
  std::cout << COUT_GTEST_BLU << "double rounding diffs: " << countDiffBetweenDoubleInteger << ANSI_TXT_DFT << std::endl;
}


TEST(GradidoUnitTest, toStringFast_RandomExact)
{
  std::mt19937_64 rng(42);

  std::uniform_int_distribution<int64_t> intDist(-1'000'000'000, 1'000'000'000);
  std::uniform_int_distribution<int> fracDist(0, 9999);
  constexpr int bufferSize = 32;
  char buffer[bufferSize];

  for (int i = 0; i < 10'000; ++i)
  {
    int64_t integerPart = intDist(rng);
    int fractionalPart = fracDist(rng);

    // Handle sign correctly
    bool negative = integerPart < 0;
    int64_t absInteger = std::abs(integerPart);

    // Build exact int64 value (GradidoCent)
    int64_t raw = absInteger * 10000 + fractionalPart;
    if (negative) raw = -raw;

    auto v = raw;

    // Build exact string
    std::stringstream ss;
    if (negative) ss << "-";
    ss << absInteger << "."
      << std::setw(4) << std::setfill('0') << fractionalPart;

    std::string fullStr = ss.str();

    // --- toString Tests ---
    for (int precision = 0; precision <= 4; ++precision) {
      grdd_unit_to_string(buffer, bufferSize, v, precision);
      
      std::string ref = fullStr;

      if (precision == 0) {
        int64_t roundedAbsInteger = absInteger;
        if (fractionalPart >= 5000) {
          ++roundedAbsInteger;
        }
        ref = (negative ? "-" : "") + std::to_string(roundedAbsInteger);
      }
      else {
        int64_t factor = pow(10.0, 4 - precision);
        int64_t rounded = (raw >= 0)
          ? (raw + factor / 2) / factor * factor
          : (raw - factor / 2) / factor * factor;

        int64_t integerPart = rounded / 10000;
        int64_t fractional = std::llabs(rounded % 10000);

        std::stringstream ss2;
        ss2 << integerPart;

        if (precision > 0) {
          ss2 << '.'
            << std::setw(precision)
            << std::setfill('0')
            << (fractional / factor);
        }
        ref = ss2.str();
      }

      ASSERT_STREQ(buffer, ref.c_str())
        << "raw=" << raw
        << " precision=" << precision
        << " full=" << fullStr
        << " i=" << i;
    }

    // --- fromString Roundtrip ---
    grdd_unit parsed;
    EXPECT_TRUE(grdd_unit_from_string(&parsed, fullStr.c_str()));

    ASSERT_EQ(parsed, raw)
      << "Parsing failed for " << fullStr;
  }
}


#include <atomic>
#include <thread>
#include <deque>

struct ThreadResult {
  int64_t exactMatches = 0;
  int64_t diffByOne = 0;
  int64_t diffByTen = 0;
  int64_t diffByHundred = 0;
  int64_t diffByOther = 0;
  std::deque<std::tuple<int64_t, int64_t, int64_t>> errors; // amount, duration, diff
};

TEST(GradidoUnitTest, testManyCasesDecayRevertDecayRandom)
{
  constexpr int64_t NUM_SAMPLES = 5000000; // 500k test cases
  unsigned int NUM_THREADS = std::thread::hardware_concurrency();
  constexpr int64_t MAX_AMOUNT_CENT = 1'000'000ll * 1000ll; // 1M Gradido * 10000 Cent = 1e13 Cent
  constexpr int64_t MAX_DURATION_SECONDS = 60ll * 60ll * 24ll * 90ll; // 90 Days in seconds

  std::atomic<int64_t> totalTests{ 0 };
  std::atomic<int64_t> exactMatches{ 0 };
  std::atomic<int64_t> diffByOne{ 0 };
  std::atomic<int64_t> diffByOther{ 0 };
  std::atomic<int64_t> diffByExactTenThousandth{ 0 }; // 0.0001 corresponds to 1 Cent difference in the Cent range

  // Mutex only for outputting error examples, not for counting
  std::mutex coutMutex;
  std::vector<std::thread> threads;

  auto worker = [&](int threadId) {
    // Each thread gets its own random generator
    std::random_device rd;
    std::mt19937_64 gen(rd() + threadId);
    std::uniform_int_distribution<int64_t> amountDist(1, MAX_AMOUNT_CENT);
    std::uniform_int_distribution<int64_t> durationDist(1, MAX_DURATION_SECONDS);

    for (int64_t i = 0; i < NUM_SAMPLES / NUM_THREADS; ++i) {
      int64_t amountCent = amountDist(gen);
      int64_t duration = durationDist(gen);

      // Ignore duration == 0, since decay is then identity
      if (duration == 0 || amountCent == 0) continue;

      grdd_unit original = amountCent;
      grdd_unit amountWithBuff = grdd_unit_calculate_decay(original, -duration);
      grdd_unit decayed = grdd_unit_calculate_decay(amountWithBuff, duration);

      totalTests++;

      if (original == decayed) {
        exactMatches++;
      }
      else {
        grdd_unit diff;
        if (original > decayed) {
          diff = original - decayed;
        }
        else {
          diff = decayed - original;
        }
        if (diff == 1) {
          diffByOne++;
        }
        else {
          diffByOther++;
        }

        // Specifically check if the difference is 0.0001 (i.e., 1 Cent in the internal format)

        // Optional: We log some examples
        if (diff != 0) {
          std::lock_guard<std::mutex> lock(coutMutex);
          // Only output every 1000th example, otherwise it becomes too much
          static int sampleCounter = 0;
          if (sampleCounter++ % 1000 == 0) 
          {
            constexpr int bufferSize = 32;
            char buffer[bufferSize];
            EXPECT_GT(grdd_unit_to_string(buffer, bufferSize, original, 4), 0);
            std::cout << "Amount: " << buffer << ", Duration: " << duration << "s";
            EXPECT_GT(grdd_unit_to_string(buffer, bufferSize, amountWithBuff, 4), 0);
            std::cout << ", amountWithBuff: " << buffer;
            EXPECT_GT(grdd_unit_to_string(buffer, bufferSize, decayed, 4), 0);
            std::cout << ", decayed: " << buffer;
            EXPECT_GT(grdd_unit_to_string(buffer, bufferSize, diff, 4), 0);
            std::cout << ", Diff: " << buffer << std::endl;
          }
        }
      }
    }
    };

  // Start threads
  for (unsigned int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back(worker, t);
  }

  // Wait for all threads
  for (auto& th : threads) {
    th.join();
  }

  // Output results
  std::cout << "\n=== Sample Results (" << totalTests << " Tests) ===" << std::endl;
  std::cout << "Exact match: " << exactMatches << " ("
    << (100.0 * exactMatches / totalTests) << "%)" << std::endl;
  std::cout << "Difference by +-1 Cent (0.0001 GDD): " << diffByOne << " ("
    << (100.0 * diffByOne / totalTests) << "%)" << std::endl;
  std::cout << "Other difference: " << diffByOther << " ("
    << (100.0 * diffByOther / totalTests) << "%)" << std::endl;

  EXPECT_EQ(exactMatches, totalTests);
}


TEST(GradidoUnitTest, testManyCasesDecayRevertDecay)
{
  constexpr int bufferSize = 32;
  char buffer[bufferSize];
  grdu_mono_timer timeUsed;
  grdu_mono_timer_reset(&timeUsed);
  unsigned int NUM_THREADS = std::thread::hardware_concurrency();

  // We sample logarithmically across the value range to cover all orders of magnitude.
  // Because errors in floating point arithmetic often depend on the order of magnitude.
  std::deque<int64_t> amountSamples;
  std::deque<int64_t> durationSamples;

  // 1. Amounts: From 1 Cent to 10 billion Gradido, logarithmically distributed
  for (int64_t exp = 0; exp <= 14; ++exp) { // 10^0 = 1 Cent to 10^14 Cent = 10^10 GDD
    int64_t base = static_cast<int64_t>(std::pow(10.0, exp));
    for (int64_t mul = 1; mul <= 9; ++mul) {
      amountSamples.push_back(mul * base);
    }
  }
  // Also some odd values, near overflows
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 10000); // Maximum GDD value in Cent
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 2);
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 3);
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 4);
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 5);
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 10);
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 100);
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 500);
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 1000);
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 2000);
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 5000);
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 9000);
  amountSamples.push_back(std::numeric_limits<int64_t>::max() / 9900);
  amountSamples.push_back(70000000000001);
  amountSamples.push_back(69999999999999);
  for (int64_t i = 69000000000000; i < 69000000800001; i++) {
    amountSamples.push_back(i);
  }
  amountSamples.push_back(69000000000000);
  amountSamples.push_back(12345678901234LL); // Just because it's pretty



  constexpr int64_t SECONDS_PER_YEAR = 31556952; // 365.2425 days

  for (int64_t exp = 0; exp <= 6; ++exp) { // 10^0 = 1s to 10^6 seconds ~ 115 days
    int64_t base = static_cast<int64_t>(std::pow(10.0, exp));
    for (int64_t mul = 1; mul <= 9; ++mul) {
      durationSamples.push_back(mul * base);
    }
  }
  // Some typical durations
  durationSamples.push_back(60);                      // 1 minute
  durationSamples.push_back(3600);                    // 1 hour
  durationSamples.push_back(86400);                   // 1 day
  durationSamples.push_back(86400ll * 30ll);              // ~1 month
  durationSamples.push_back(86400ll * 60ll);              // ~2 months
  durationSamples.push_back(86400ll * 90ll);              // ~3 months

  durationSamples.push_back(86400 * 14);                   // 1 day

  // Remove duplicates and sort for a good feeling
  std::sort(amountSamples.begin(), amountSamples.end());
  amountSamples.erase(std::unique(amountSamples.begin(), amountSamples.end()), amountSamples.end());
  std::sort(durationSamples.begin(), durationSamples.end());
  durationSamples.erase(std::unique(durationSamples.begin(), durationSamples.end()), durationSamples.end());

  // Remove 0 from durations (makes no sense)
  durationSamples.erase(std::remove(durationSamples.begin(), durationSamples.end(), 0), durationSamples.end());

  size_t totalTests = amountSamples.size() * durationSamples.size();
  std::cout << "Testing " << amountSamples.size() << " amounts x " << durationSamples.size()
    << " durations = " << totalTests << " combinations." << std::endl;

  grdu_mono_timer_string(buffer, bufferSize, &timeUsed);
  std::cout << "Time for preparations: " << buffer << std::endl;

  std::vector<ThreadResult> threadResults(NUM_THREADS);
  std::vector<std::thread> threads;

  // Distribute the work evenly across threads
  size_t chunkSize = (totalTests + NUM_THREADS - 1) / NUM_THREADS;
  std::atomic<int64_t> smallestError = std::numeric_limits<int64_t>::max();
  std::atomic<int64_t> smallestErrorDuration;

  // 9223372036854775807
       // 7'000'000'000,0000

  for (unsigned int t = 0; t < NUM_THREADS; ++t)
  {
    threads.emplace_back([&, t]()
      {
        size_t startIdx = t * chunkSize;
        size_t endIdx = std::min(startIdx + chunkSize, totalTests);

        auto& res = threadResults[t];

        for (size_t idx = startIdx; idx < endIdx; ++idx) {
          // Calculate 2D index from flat index
          size_t amountIdx = idx / durationSamples.size();
          size_t durationIdx = idx % durationSamples.size();

          // Skip if index is out of bounds (should not happen)
          if (amountIdx >= amountSamples.size()) continue;

          int64_t amountCent = amountSamples[amountIdx];
          int64_t duration = durationSamples[durationIdx];

          grdd_unit original = amountCent;
          grdd_unit buffed = grdd_unit_calculate_decay(original, -duration);
          grdd_unit decayed = grdd_unit_calculate_decay(buffed, duration);

          int64_t diff;
          if (original > decayed) {
            diff = original - decayed;
          }
          else {
            diff = decayed - original;
          }
          if (diff == 0) {
            ++res.exactMatches;
          }
          else if (diff == 1) {
            ++res.diffByOne;
          }
          else if (diff <= 10) {
            ++res.diffByTen;
          }
          else if (diff <= 100) {
            ++res.diffByHundred;
          } 
          else {
            res.diffByOther++;

            if (amountCent < smallestError) {
              smallestError = amountCent;
              smallestErrorDuration = duration;
            }

            // Store each error for later analysis (or limit the number)
            if (res.errors.size() < 1000) { // Do not store too many
              res.errors.emplace_back(amountCent, duration, diff);
            }
          }
        }
      }
    );
  }

  for (auto& th : threads) {
    th.join();
  }

  // Aggregate results
  int64_t totalExact = 0, totalDiffOne = 0, totalDiffTen = 0, totalDiffHoundred = 0, totalDiffOther = 0;
  std::vector<std::tuple<int64_t, int64_t, int64_t>> allErrors;

  for (const auto& res : threadResults) {
    totalExact += res.exactMatches;
    totalDiffOne += res.diffByOne;
    totalDiffTen += res.diffByTen;
    totalDiffHoundred += res.diffByHundred;
    totalDiffOther += res.diffByOther;
    for (const auto& err : res.errors) {
      if (allErrors.size() < 1000) {
        allErrors.push_back(err);
      }
    }
  }

  // Output
  std::cout << "\n=== Results of uniform grid tests (" << totalTests << " combinations) ===" << std::endl;
  std::cout << "Exact matches: " << totalExact << " ("
    << (100.0 * totalExact / totalTests) << "%)" << std::endl;
  std::cout << "Difference +-1 Cent (0.0001 GDD): " << totalDiffOne << " ("
    << (100.0 * totalDiffOne / totalTests) << "%)" << std::endl;
  std::cout << "Difference [-10;10] Cent (0.001 GDD): " << totalDiffTen << " ("
    << (100.0 * totalDiffTen / totalTests) << "%)" << std::endl;
  std::cout << "Difference [-100;100] Cent (0.01 GDD): " << totalDiffHoundred << " ("
    << (100.0 * totalDiffHoundred / totalTests) << "%)" << std::endl;
  std::cout << "Other differences: " << totalDiffOther << " ("
    << (100.0 * totalDiffOther / totalTests) << "%)" << std::endl;
  std::cout << "Smallest Error: " << smallestError << ", " << smallestErrorDuration << " seconds " << std::endl;
  
  // Show the first 20 errors as examples
  std::cout << "\nExample errors (amount in Cent, duration in seconds, difference in Cent):" << std::endl;
  std::set<int64_t> postedGdd;
  for (size_t i = 0; i < allErrors.size(); ++i) {
    const auto& [amt, dur, diff] = allErrors[i];
    if (!postedGdd.insert(amt).second) {
      continue;
    }
    std::cout << "  " << amt << " Cent (" << (amt / 10000.0) << " GDD) | "
      << dur << "s | Diff: " << diff << " Cent = " << (diff / 10000.0) << " GDD" << std::endl;
    if (postedGdd.size() > 20) {
      break;
    }
  }

  EXPECT_LE((100.0 * totalDiffOther / totalTests), 0.01);
}


TEST(GradidoUnitTest, testPrecisionDifferentTimeTransactions)
{
  using namespace std::chrono;

  // --- Define time points ---
  grdd_timestamp_seconds start = 0;
  grdd_timestamp_seconds t2 = 60 * 60 * 24 * 30;   // +30 days
  grdd_timestamp_seconds t3 = 60 * 60 * 24 * 90;   // +90 days
  grdd_timestamp_seconds end = 60 * 60 * 24 * 365;  // +1 year

  // --- Large values near int64 limit ---
  // int64 max ~9.22e18 -> we stay a bit below because of *10000
  int64_t maxSafeCent = std::numeric_limits<int64_t>::max() / 2;

  grdd_unit startAmount = maxSafeCent;
  grdd_unit minusAmount = 100 * 10000; // -100 GDD
  grdd_unit plusAmount = 500 * 10000; // +500 GDD

  // --- Variant 1: Step-by-step simulation ---
  grdd_unit step = startAmount;

  // decay to t2
  step = grdd_unit_calculate_decay(step, t2 - start);

  // -100 GDD at t2
  step -= minusAmount;

  // decay to t3
  step = grdd_unit_calculate_decay(step, t3 - t2);

  // +500 GDD at t3
  step += plusAmount;

  // decay to end
  step = grdd_unit_calculate_decay(step, end - t3);


  // --- Variant 2: Reference calculation ---
  grdd_unit ref = grdd_unit_calculate_decay(startAmount, end - start);

  // -100 GDD -> from t2 to end decayed
  grdd_unit minusDecayed = grdd_unit_calculate_decay(minusAmount, end - t2);
  ref -= minusDecayed;

  // +500 GDD -> from t3 to end decayed
  grdd_unit plusDecayed = grdd_unit_calculate_decay(plusAmount, end - t3);
  ref += plusDecayed;


  // --- Comparison ---
  // Not exact due to rounding -> Tolerance via GradidoCent
  int64_t stepCent = step;
  int64_t refCent = ref;

  int64_t diff = std::llabs(stepCent - refCent);

  // Tolerance: 1 Cent (0.0001 GDD)
  EXPECT_LE(diff, 1)
    << "Mismatch between step-by-step and reference calculation. "
    << "step=" << stepCent << " ref=" << refCent << " diff=" << diff;
}

TEST(GradidoUnitTest, testOverflowProvocation)
{
  using namespace std::chrono;

  // --- Time ---
  grdd_timestamp_seconds start = 0;

  // Negative time period -> Compound Interest (GROWTH)
  grdd_timestamp_seconds hugeNegativeDuration = -60 * 60 * 24 * 365 * 10; // -10 years

  // --- Start value near limit ---
  int64_t nearMax = std::numeric_limits<int64_t>::max() / 10000 - 1;
  grdd_unit value = nearMax;

  bool overflowDetected = false;

  
  // Apply multiple times -> escalating growth
  for (int i = 0; i < 10; ++i) {
    value = grdd_unit_calculate_decay(value, hugeNegativeDuration);

    EXPECT_GT(value, 0);
  }
}

const uint64_t secondsPerYear = 31556952;
TEST(TestUnit, decayAfterYear)
{
  grdd_unit gdd;
  grdd_unit_from_string(&gdd, "1000");
  grdd_unit result = grdd_unit_calculate_decay(gdd, secondsPerYear);
  char buffer[30];
  grdd_unit_to_string(buffer, 10, result, 4);
  EXPECT_STREQ(buffer, "500.0000");
  grdd_unit_to_string(buffer, 30, result, 2);
  EXPECT_STREQ(buffer, "500.00");
}

