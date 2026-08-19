#include "bench_report.h"
#include "gradido_blockchain_core/data/unit.h"
#include "gradido_blockchain_core/data/wire/hiero.h"
#include "gradido_blockchain_core/utils/converter.h"
#include "hostmem/mono_timer.h"

#include "r128/r128.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_VALUES_COUNT 1000
#define STRING_BUFFER_SIZE 32
uint64_t testValues[TEST_VALUES_COUNT];
char benchBuffer[STRING_BUFFER_SIZE];

static uint64_t getNextTestValue() {
  static int cursor = 0;
  uint64_t result = testValues[cursor++];
  if (cursor >= TEST_VALUES_COUNT) { cursor = 0; }
  return result;
}

static void test_snprintf_integer(int stepCount) {
  for (int i = 0; i < stepCount; ++i) {
    snprintf(benchBuffer, STRING_BUFFER_SIZE, "%" PRIu64, getNextTestValue());
  }
}

static void test_r128_integer(int stepCount) {
  R128 v = {.lo = 0, .hi = 0};
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

static void test_unit_fixed(int stepCount) {
  grdd_unit gdd;
  for (int i = 0; i < stepCount; ++i) {
    gdd = getNextTestValue();
    grdd_unit_to_string(benchBuffer, STRING_BUFFER_SIZE, gdd, 4);
  }
}

static void test_unit_round(int stepCount) {
  grdd_unit r;
  for (int i = 0; i < stepCount; ++i) { grdd_unit_round_to_precision(&r, getNextTestValue(), 2); }
}

static void test_calculate_decay(int stepCount) {
  for (int i = 0; i < stepCount; ++i) {
    grdd_unit_calculate_decay(abs((int64_t)getNextTestValue()), getNextTestValue() % 31556952 * 10);
  }
}

grdw_hiero_transaction_id transactionId = {
    .transactionValidStart = {.seconds = 171627121, .nanos = 2912},
    .accountID = {.shardNum = 0, .realmNum = 0, .accountNum = 1233}
};
static void test_hiero_transaction_id_to_string_snprintf(int stepCount) {
  char buffer[128];
  for (int i = 0; i < stepCount; ++i) {
    snprintf(
        buffer, 128, "%lld.%lld.%lld@%lld.%09d", transactionId.accountID.shardNum,
        transactionId.accountID.realmNum, transactionId.accountID.accountNum,
        transactionId.transactionValidStart.seconds, transactionId.transactionValidStart.nanos
    );
  }
}

static void test_hiero_transaction_id_to_string(int stepCount) {
  char buffer[128];
  for (int i = 0; i < stepCount; ++i) {
    grdw_hiero_transaction_id_to_string(buffer, 128, &transactionId);
  }
}

#ifdef USE_SODIUM
#include "sodium.h"

/*
 * These four conversions live in hostmem now, which links no crypto library and therefore has
 * nothing to compare itself against. That comparison is the reason this section stayed behind:
 * the two functions below are what hostmem's uuid pair used to be, back when it was built on
 * libsodium, kept here so the rows measure the difference instead of asserting it.
 *
 * The hex section further down needs no such copy -- grdu_secret_to_hex and
 * grdu_secret_from_hex are libsodium and ship in this library, so both of its rows call
 * something real.
 */
static hostmem_result uuid_from_string_sodium(uint8_t *uuid, const char *uuid_string) {
  if (!uuid || !uuid_string) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (strlen(uuid_string) != 36) { return HOSTMEM_ERROR_INVALID_PARAM; }

  char hex[33];
  memcpy(hex, uuid_string, 8);
  memcpy(hex + 8, uuid_string + 9, 4);
  memcpy(hex + 12, uuid_string + 14, 4);
  memcpy(hex + 16, uuid_string + 19, 4);
  memcpy(hex + 20, uuid_string + 24, 12);
  hex[32] = '\0';

  size_t bin_len = 0;
  if (sodium_hex2bin(uuid, 16, hex, 32, NULL, &bin_len, NULL) != 0) {
    return HOSTMEM_ERROR_ENCODE_FAILED;
  }
  if (bin_len != 16) { return HOSTMEM_ERROR_INVALID_PARAM; }
  return HOSTMEM_SUCCESS;
}

static void uuid_to_string_sodium(
    char *result_buffer, const uint8_t uuid[HOSTMEM_UUID_BINARY_SIZE]
) {
  char hex[33];
  sodium_bin2hex(hex, sizeof(hex), uuid, 16);
  memcpy(result_buffer, hex, 8);
  result_buffer[8] = '-';
  memcpy(result_buffer + 9, hex + 8, 4);
  result_buffer[13] = '-';
  memcpy(result_buffer + 14, hex + 12, 4);
  result_buffer[18] = '-';
  memcpy(result_buffer + 19, hex + 16, 4);
  result_buffer[23] = '-';
  memcpy(result_buffer + 24, hex + 20, 12);
  result_buffer[36] = '\0';
}

/*
 * Both directions read their input from these samples rather than from one fixed uuid: every
 * implementation here indexes a table with the data itself, and a single value would exercise
 * one path through it and time the cache rather than the code.
 */
#define UUID_SAMPLE_COUNT 64
static uint8_t uuidSamples[UUID_SAMPLE_COUNT][HOSTMEM_UUID_BINARY_SIZE];
static char uuidSampleStrings[UUID_SAMPLE_COUNT][37];

/* Written where the compiler cannot see they go unread, so no row loses work the others do. */
uint8_t benchUuidBinary[HOSTMEM_UUID_BINARY_SIZE];
char benchUuidString[37];

static void prepare_uuid_samples(void) {
  for (int i = 0; i < UUID_SAMPLE_COUNT; ++i) {
    uint64_t halves[2] = {getNextTestValue(), getNextTestValue()};
    memcpy(uuidSamples[i], halves, HOSTMEM_UUID_BINARY_SIZE);
    /* the reference implementation writes them, so the parsers are timed on inputs neither of
       them produced */
    uuid_to_string_sodium(uuidSampleStrings[i], uuidSamples[i]);
  }
}

static void test_uuid_from_string_sodium(int stepCount) {
  for (int i = 0; i < stepCount; ++i) {
    uuid_from_string_sodium(benchUuidBinary, uuidSampleStrings[i % UUID_SAMPLE_COUNT]);
  }
}

static void test_uuid_from_string(int stepCount) {
  for (int i = 0; i < stepCount; ++i) {
    hostmem_uuid_from_string(benchUuidBinary, uuidSampleStrings[i % UUID_SAMPLE_COUNT]);
  }
}

static void test_uuid_to_string_sodium(int stepCount) {
  for (int i = 0; i < stepCount; ++i) {
    uuid_to_string_sodium(benchUuidString, uuidSamples[i % UUID_SAMPLE_COUNT]);
  }
}

static void test_uuid_to_string(int stepCount) {
  for (int i = 0; i < stepCount; ++i) {
    hostmem_uuid_to_string(benchUuidString, uuidSamples[i % UUID_SAMPLE_COUNT]);
  }
}

/*
 * The same comparison for the general hex conversions: hostmem's fast pair against the constant
 * time pair this library keeps for secrets. 32 bytes is the size that actually occurs here -- a
 * hash or a public key -- and the ratio holds from one byte up to a few thousand.
 *
 * The two directions do not come out alike, and the reason is worth keeping next to the
 * numbers: sodium_bin2hex is a branchless map over the bytes, which a compiler can vectorise,
 * while sodium_hex2bin carries a state machine and a bounds check per nibble and cannot be.
 */
#define HEX_SAMPLE_BYTES 32
static uint8_t hexSampleBinary[HEX_SAMPLE_BYTES];
static char hexSampleString[HEX_SAMPLE_BYTES * 2 + 1];
uint8_t benchHexBinary[HEX_SAMPLE_BYTES];
char benchHexString[HEX_SAMPLE_BYTES * 2 + 1];

static void prepare_hex_samples(void) {
  for (int i = 0; i < HEX_SAMPLE_BYTES; ++i) {
    hexSampleBinary[i] = (uint8_t)(getNextTestValue() >> 24);
  }
  sodium_bin2hex(hexSampleString, sizeof(hexSampleString), hexSampleBinary, HEX_SAMPLE_BYTES);
}

/* Both rows call what the library actually ships, so the figures in converter.h can be
   rechecked here rather than trusted. */
static void test_binary_to_hex_secret(int stepCount) {
  hostmem_memory_block block = {hexSampleBinary, HEX_SAMPLE_BYTES};
  for (int i = 0; i < stepCount; ++i) { grdu_secret_to_hex(benchHexString, &block); }
}

static void test_binary_to_hex(int stepCount) {
  hostmem_memory_block block = {hexSampleBinary, HEX_SAMPLE_BYTES};
  for (int i = 0; i < stepCount; ++i) { hostmem_binary_to_hex(benchHexString, &block); }
}

static void test_binary_from_hex_secret(int stepCount) {
  for (int i = 0; i < stepCount; ++i) { grdu_secret_from_hex(benchHexBinary, hexSampleString); }
}

static void test_binary_from_hex(int stepCount) {
  for (int i = 0; i < stepCount; ++i) { hostmem_binary_from_hex(benchHexBinary, hexSampleString); }
}

#endif // USE_SODIUM

static void test_r128_round(int stepCount) {
  R128 v = {.lo = 0, .hi = 0};
  R128 div;
  r128FromInt(&div, 100);

  for (int i = 0; i < stepCount; ++i) {
    v.hi = getNextTestValue();
    r128Div(&v, &v, &div);
    r128Round(&v, &v);
    r128Mul(&v, &v, &div);
  }
}

static void prepare_test_data() {
  srand(12812);
  for (int i = 0; i < TEST_VALUES_COUNT; ++i) {
    testValues[i] = ((uint64_t)rand() << 48) ^ ((uint64_t)rand() << 32) ^ ((uint64_t)rand() << 16) ^
                    (uint64_t)rand();
  }
#ifdef USE_SODIUM
  prepare_uuid_samples();
  prepare_hex_samples();
#endif // USE_SODIUM
}

int main(void) {
  hostmem_mono_timer_init();
  hostmem_mono_timer timeUsed;

  hostmem_mono_timer_reset(&timeUsed);
  prepare_test_data();
  bench_prepared(timeUsed);

  const int stepCount = TEST_VALUES_COUNT * 1000;

  bench_section("integer to string");
  bench_step(test_snprintf_integer, stepCount, "  snprintf", "conversion");
  bench_step(test_r128_integer, stepCount, "  r128", "conversion");

  bench_section("fixed point to string");
  bench_step(test_unit_fixed, stepCount, "  grdd unit", "conversion");

  bench_section("rounding");
  bench_step(test_unit_round, stepCount, "  grdd unit", "operation");
  bench_step(test_r128_round, stepCount, "  r128", "operation");
  bench_step(test_calculate_decay, stepCount, "  decay over a random timespan", "operation");

  bench_section("hiero transaction id to string");
  bench_step(test_hiero_transaction_id_to_string_snprintf, stepCount, "  snprintf", "conversion");
  bench_step(test_hiero_transaction_id_to_string, stepCount, "  hostmem", "conversion");

#ifdef USE_SODIUM
  bench_section("uuid from string");
  bench_step(test_uuid_from_string_sodium, stepCount, "  libsodium", "conversion");
  bench_step(test_uuid_from_string, stepCount, "  hostmem", "conversion");

  bench_section("uuid to string");
  bench_step(test_uuid_to_string_sodium, stepCount, "  libsodium", "conversion");
  bench_step(test_uuid_to_string, stepCount, "  hostmem", "conversion");

  bench_section("32 bytes to hex");
  bench_step(test_binary_to_hex_secret, stepCount, "  secret, libsodium", "conversion");
  bench_step(test_binary_to_hex, stepCount, "  fast, hostmem", "conversion");

  bench_section("32 bytes from hex");
  bench_step(test_binary_from_hex_secret, stepCount, "  secret, libsodium", "conversion");
  bench_step(test_binary_from_hex, stepCount, "  fast, hostmem", "conversion");
#endif // USE_SODIUM

  bench_total(timeUsed, stepCount, "value");

  return 0;
}
