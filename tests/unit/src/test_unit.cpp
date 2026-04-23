#include <gtest/gtest.h>
#include <math.h>
#include "gradido_blockchain_core/data/unit.h"


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

