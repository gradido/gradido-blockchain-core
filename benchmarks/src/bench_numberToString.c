#include "gradido_blockchain_core/utils/converter.h"
#include "gradido_blockchain_core/utils/duration.h"
#include "gradido_blockchain_core/utils/mono_timer.h"
#include "gradido_blockchain_core/data/unit.h"

#include "r128/r128.h"

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#define TEST_VALUES_COUNT 1000
#define STRING_BUFFER_SIZE 32
uint64_t testValues[TEST_VALUES_COUNT];
char benchBuffer[STRING_BUFFER_SIZE];

static uint64_t getNextTestValue() {
  static int cursor = 0;
  uint64_t result = testValues[cursor++];
  if (cursor >= TEST_VALUES_COUNT) {
    cursor = 0;
  }
  return result;
}

static void test_snprintf_integer(int stepCount)
{
  for (int i = 0; i < stepCount; ++i) {
    snprintf(benchBuffer, STRING_BUFFER_SIZE, "%" PRIu64, getNextTestValue());
  }
}

static void test_lr_algo_integer(int stepCount)
{
  for (int i = 0; i < stepCount; ++i) {
    grdu_uint64_to_string(benchBuffer, STRING_BUFFER_SIZE, getNextTestValue());
  }
}

static void test_r128_integer(int stepCount)
{
  R128 v = {
    .lo = 0,
    .hi = 0
  };
  int precision = 4;
  R128ToStringFormat opt = {
      .sign = R128ToStringSign_Default,
      .width = precision + 2,
      .precision = precision,
      .zeroPad = 0,
      .decimal = precision > 0,
      .leftAlign = 0,
  };
  for (int i = 0; i < stepCount; ++i) {
    v.hi = getNextTestValue();
    r128ToStringOpt(benchBuffer, STRING_BUFFER_SIZE, &v, &opt);
  }
}

static void test_unit_fixed(int stepCount)
{
  grdd_unit gdd;
  for (int i = 0; i < stepCount; ++i) {
    gdd = getNextTestValue();
    grdd_unit_to_string(benchBuffer, STRING_BUFFER_SIZE, gdd, 4);
  }
}

static void test_duration_to_string(int stepCount)
{
  for (int i = 0; i < stepCount; ++i) {
    grdu_duration_string(benchBuffer, STRING_BUFFER_SIZE, getNextTestValue(), 4);
  }
}

static void test_unit_round(int stepCount)
{
  grdd_unit r;
  for (int i = 0; i < stepCount; ++i) {
    grdd_unit_round_to_precision(&r, getNextTestValue(), 2);
  }
}

static void test_r128_round(int stepCount)
{
  R128 v = {
    .lo = 0,
    .hi = 0
  };
  R128 div;
  r128FromInt(&div, 100);

  for (int i = 0; i < stepCount; ++i) {
    v.hi = getNextTestValue();
    r128Div(&v, &v, &div);
    r128Round(&v, &v);
    r128Mul(&v, &v, &div);
  }
}

static void prepare_test_data()
{
  srand(12812);
  for (int i = 0; i < TEST_VALUES_COUNT; ++i) {
    testValues[i] =
      ((uint64_t)rand() << 48) ^
      ((uint64_t)rand() << 32) ^
      ((uint64_t)rand() << 16) ^
      (uint64_t)rand();
  }
}

static void bench_step(void (*func_ptr)(int), int stepCount, const char* name)
{
  char buffer[STRING_BUFFER_SIZE*2];
  grdu_mono_timer timeUsed;
  grdu_mono_timer_reset(&timeUsed);
  func_ptr(stepCount);
  grdu_mono_timer_string(buffer, STRING_BUFFER_SIZE*2, &timeUsed);
  printf("%s: %s\n", name, buffer);
}

int main(void)
{
  char buffer[STRING_BUFFER_SIZE];
  grdu_mono_timer_init();
  grdu_mono_timer timeUsed;

  grdu_mono_timer_reset(&timeUsed);
  prepare_test_data();
  grdu_mono_timer_string(buffer, STRING_BUFFER_SIZE, &timeUsed);
  printf("time for prepare test data: %s\n", buffer);

  const int stepCount = TEST_VALUES_COUNT * 1000;

  bench_step(test_snprintf_integer, stepCount, "snprintf integer");
  bench_step(test_lr_algo_integer, stepCount, "lr algo integer");
  bench_step(test_r128_integer, stepCount, "r128 integer");
  bench_step(test_unit_fixed, stepCount, "grdd unit fixed point integer");
  bench_step(test_duration_to_string, stepCount, "duration to string r128");
  bench_step(test_unit_round, stepCount, "unit round");
  bench_step(test_r128_round, stepCount, "r128 round");

  grdu_mono_timer_string(buffer, STRING_BUFFER_SIZE, &timeUsed);
  printf("all benchmarks: %s, stepSize: %d\n", buffer, stepCount);

  return 0;
}