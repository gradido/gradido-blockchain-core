#include <gtest/gtest.h>
#include "gradido_blockchain_core/data/unit.h"


TEST(TestUnit, fromToString)
{
  const char* testString = "1029.1021";
  grdd_unit value;
  grdd_unit_from_string(&value, testString, nullptr);
  EXPECT_EQ(value.hi, 1029);
  EXPECT_EQ(value.lo, 1883412569925745219);
  char buffer[45];
  grdd_unit_to_string(buffer, 45, &value, 4);
  EXPECT_STREQ(testString, buffer);
  grdd_unit_to_string(buffer, 45, &value, 8);
  EXPECT_STREQ("1029.10210000", buffer);
  grdd_unit_to_string(buffer, 45, &value, 16);
  EXPECT_STREQ("1029.1021000000000000", buffer);
}


TEST(TestUnit, compare)
{
  grdd_unit value1 = {1, 0};
  grdd_unit value2 = {1, 1};
  EXPECT_FALSE(grdd_unit_compare(&value1, &value1));
  EXPECT_EQ(grdd_unit_compare(&value2, &value1), 1);
  EXPECT_EQ(grdd_unit_compare(&value1, &value2), -1);
}

TEST(TestUnit, add)
{
  grdd_unit value1 = {1, 0};
  grdd_unit value2 = {1, 1};
  grdd_unit result;
  grdd_unit expectedResult = { 2, 1 };
  grdd_unit_add(&result, &value1, &value2);
  EXPECT_EQ(grdd_unit_compare(&result, &expectedResult), 0);
}

TEST(TestUnit, sub)
{
  grdd_unit value1 = {2, 1};
  grdd_unit value2 = {1, 1};
  grdd_unit result;
  grdd_unit expectedResult = {1, 0};
  grdd_unit_subtract(&result, &value1, &value2);
  EXPECT_EQ(grdd_unit_compare(&result, &expectedResult), 0);
}

TEST(TestUnit, mul)
{
  grdd_unit value1 = {1, 1};
  grdd_unit value2 = {2, 0};
  grdd_unit result;
  grdd_unit expectedResult = { 2, 0 };
  grdd_unit_multiplicate(&result, &value1, &value2);
  EXPECT_EQ(grdd_unit_compare(&result, &expectedResult), 0);
}
const uint64_t secondsPerYear = 31556952;
TEST(TestUnit, decayAfterYear)
{
  grdd_unit gdd;
  grdd_unit_from_string(&gdd, "1000", nullptr);
  grdd_unit result;
  grdd_unit_calculate_decay(&result, &gdd, secondsPerYear);
  char buffer[30];
  grdd_unit_to_string(buffer, 10, &result, 4);
  EXPECT_STREQ(buffer, "500.0000");
  grdd_unit_to_string(buffer, 30, &result, 19);
  printf("one step after one year: %s\n", buffer);
}

TEST(TestUnit, decayAfterYearSteps)
{
  grdd_unit gdd;
  grdd_unit_from_string(&gdd, "1000", nullptr);
  grdd_unit result;
  double dResult = 1000.0;
  uint64_t stepSize = secondsPerYear / 8;
  ASSERT_EQ(stepSize * 8, secondsPerYear);
  
  auto f = pow(2.0, -1.0 / double(secondsPerYear));
  R128 fr;
  r128FromFloat(&fr, f);
  auto fs = f * 18446744073709551616.0;
  printf("%.29f\n", fs);
  

  r128Copy(&result, &gdd);
  auto factorPerStep = pow(2.0, -(double)stepSize / (double)secondsPerYear);
  for (int i = 0; i < 8; i++) {
    grdd_unit_calculate_decay(&result, &result, stepSize);
    dResult *= factorPerStep;
  }

  //grdd_unit_calculate_decay(&result, &gdd, secondsPerYear);
  char buffer[30];
  grdd_unit_to_string(buffer, 30, &result, 19);
  char buffer2[30];
  snprintf(buffer2, 30, "%.19f", dResult);
  printf("1 Jahr Decay of 1'000 GDD after 8 steps: %s, %s\n", buffer, buffer2);
}

