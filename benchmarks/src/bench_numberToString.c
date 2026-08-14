#include "bench_report.h"
#include "gradido_blockchain_core/utils/converter.h"
#include "gradido_blockchain_core/utils/duration.h"
#include "gradido_blockchain_core/utils/mono_timer.h"
#include "gradido_blockchain_core/data/unit.h"
#include "gradido_blockchain_core/data/wire/hiero.h"

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


static void test_calculate_decay(int stepCount)
{
    for (int i = 0; i < stepCount; ++i) {
        grdd_unit_calculate_decay(abs((int64_t)getNextTestValue()), getNextTestValue() % 31556952 * 10);
    }
}

grdw_hiero_transaction_id transactionId = {
    .transactionValidStart = {.seconds = 171627121, .nanos = 2912},
    .accountID = {.shardNum = 0, .realmNum = 0, .accountNum = 1233}
};
static void test_hiero_transaction_id_to_string_snprintf(int stepCount)
{
  char buffer[128];
  for (int i = 0; i < stepCount; ++i) {
    snprintf(buffer, 128, "%lld.%lld.%lld@%lld.%09d",
      transactionId.accountID.shardNum,
      transactionId.accountID.realmNum,
      transactionId.accountID.accountNum,
      transactionId.transactionValidStart.seconds,
      transactionId.transactionValidStart.nanos
    );
  }
}

static void test_hiero_transaction_id_to_string(int stepCount)
{
  char buffer[128];
  for (int i = 0; i < stepCount; ++i) {
    grdw_hiero_transaction_id_to_string(buffer, 128, &transactionId);
  }
}

#ifdef USE_SODIUM
#include "sodium.h"

static void test_uuid_from_string(int stepCount)
{
  const char expected[] = "48066a47-a02f-4596-883c-302c2b1aa1e1";
  int8_t uuid[16];
  for (int i = 0; i < stepCount; ++i) {
    grdu_uuid_from_string(uuid, expected);
  }
}

static void test_uuid_to_string(int stepCount)
{
  char buffer[37];
  for (int i = 0; i < stepCount; ++i) {
    uint64_t uuid[2] = {getNextTestValue(), getNextTestValue()};
    grdu_uuid_to_string(buffer, (uint8_t*)uuid);
  }
}


#endif //USE_SODIUM

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

int main(void)
{
  grdu_mono_timer_init();
  grdu_mono_timer timeUsed;

  grdu_mono_timer_reset(&timeUsed);
  prepare_test_data();
  bench_prepared(timeUsed);

  const int stepCount = TEST_VALUES_COUNT * 1000;

  bench_section("integer to string");
  bench_step(test_snprintf_integer, stepCount, "  snprintf", "conversion");
  bench_step(test_lr_algo_integer, stepCount, "  lr algo", "conversion");
  bench_step(test_r128_integer, stepCount, "  r128", "conversion");

  bench_section("fixed point to string");
  bench_step(test_unit_fixed, stepCount, "  grdd unit", "conversion");
  bench_step(test_duration_to_string, stepCount, "  duration, r128 backed", "conversion");

  bench_section("rounding");
  bench_step(test_unit_round, stepCount, "  grdd unit", "operation");
  bench_step(test_r128_round, stepCount, "  r128", "operation");
  bench_step(test_calculate_decay, stepCount, "  decay over a random timespan", "operation");

  bench_section("hiero transaction id to string");
  bench_step(test_hiero_transaction_id_to_string_snprintf, stepCount, "  snprintf", "conversion");
  bench_step(test_hiero_transaction_id_to_string, stepCount, "  hand written", "conversion");

#ifdef USE_SODIUM
  bench_section("uuid");
  bench_step(test_uuid_from_string, stepCount, "  from string", "conversion");
  bench_step(test_uuid_to_string, stepCount, "  to string", "conversion");
#endif // USE_SODIUM

  bench_total(timeUsed, stepCount, "value");

  return 0;
}
