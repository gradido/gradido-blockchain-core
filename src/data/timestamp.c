#include "gradido_blockchain_core/data/timestamp.h"
#include "arnm/converter.h"
#include "gradido_blockchain_core/data/types.h"
#include "gradido_blockchain_core/utils/converter.h"

#include <stdint.h>
#include <string.h>

#define NANOS_PER_SECOND 1000000000

/*
 * Adding or subtracting seconds can leave the int64_t range, which is undefined behaviour and
 * not something a later check can recover from. Pinning to the end of the range instead keeps
 * the result a value the rest of the code can reason about: still wrong, but deterministically
 * so, and never a compiler licence to assume the overflow cannot happen.
 */
static int64_t seconds_add(int64_t a, int64_t b) {
  if (b > 0 && a > INT64_MAX - b) { return INT64_MAX; }
  if (b < 0 && a < INT64_MIN - b) { return INT64_MIN; }
  return a + b;
}

static int64_t seconds_sub(int64_t a, int64_t b) {
  if (b < 0 && a > INT64_MAX + b) { return INT64_MAX; }
  if (b > 0 && a < INT64_MIN + b) { return INT64_MIN; }
  return a - b;
}

/*
 * Moves whole seconds out of @p nanos until it lands in [0, NANOS_PER_SECOND). A timestamp is
 * seconds + nanos/1e9, so this does not move the point in time — it only brings it into the
 * one shape the rest of the API expects, the same range grdd_timestamp_to_string() can print.
 * Both operands may carry an out of range nanos: nothing in the wire types bounds the field.
 */
static grdd_timestamp normalize(int64_t seconds, int64_t nanos) {
  int64_t carry = nanos / NANOS_PER_SECOND;
  int32_t rest = (int32_t)(nanos % NANOS_PER_SECOND);
  // C truncates towards zero, so a negative remainder still owes a whole second
  if (rest < 0) {
    rest += NANOS_PER_SECOND;
    carry--;
  }
  return (grdd_timestamp){.seconds = seconds_add(seconds, carry), .nanos = rest};
}

grdd_timestamp grdd_timestamp_minus(const grdd_timestamp *t1, const grdd_timestamp *t2) {
  if (!t1 || !t2) { return (grdd_timestamp){0}; }
  // nanos in int64_t: the difference of two int32_t cannot overflow it before normalizing
  return normalize(seconds_sub(t1->seconds, t2->seconds), (int64_t)t1->nanos - (int64_t)t2->nanos);
}

grdd_timestamp grdd_timestamp_plus(const grdd_timestamp *t1, const grdd_timestamp *t2) {
  if (!t1 || !t2) { return (grdd_timestamp){0}; }
  return normalize(seconds_add(t1->seconds, t2->seconds), (int64_t)t1->nanos + (int64_t)t2->nanos);
}

grdd_timestamp grdd_timestamp_from_timestamp_seconds(
    const grdd_timestamp_seconds timestamp_seconds
) {
  return (grdd_timestamp){.seconds = timestamp_seconds, .nanos = 0};
}

int64_t grdd_timestamp_get_seconds(const grdd_timestamp *timestamp) {
  if (!timestamp) { return 0; }
  return timestamp->seconds;
}

int32_t grdd_timestamp_get_nanos(const grdd_timestamp *timestamp) {
  if (!timestamp) { return 0; }
  return timestamp->nanos;
}

/*
 * The fractional part is always written as exactly 9 digits, so only nanos in [0, 999999999]
 * have a representation here. Out of range it is not a formatting question but broken data:
 * a negative value drove the zero padding below zero, and memset read that as a length near
 * SIZE_MAX. Nothing in the wire types bounds the field, so it is bounded here.
 */
static bool nanos_representable(int32_t nanos) {
  return nanos >= 0 && nanos < NANOS_PER_SECOND;
}

size_t grdd_timestamp_calculate_string_size(const grdd_timestamp *timestamp) {
  if (!timestamp || !nanos_representable(timestamp->nanos)) { return 0; }
  // always 9 for nano seconds, and pad with 0
  return arnm_int64_to_string_size(timestamp->seconds) + 9 + 1;
}

size_t grdd_timestamp_to_string(char *buffer, size_t buffer_size, const grdd_timestamp *timestamp) {
  if (!buffer || !buffer_size || !timestamp) { return 0; }
  if (!nanos_representable(timestamp->nanos)) { return 0; }

  size_t seconds_size = arnm_int64_to_string_size(timestamp->seconds);
  size_t nanos_size = arnm_int64_to_string_size(timestamp->nanos);
  size_t result_size = seconds_size + 1 + 9;
  // buffer_size counts the terminator, the way snprintf and arnm's converters count it, so a
  // buffer of exactly result_size is one byte short rather than an exact fit -- the writes below
  // close the run with a '\0'. The return stays the character count, which is what a caller
  // sizing a buffer adds one to.
  if (buffer_size < result_size + 1) { return result_size; }

  arnm_int64_to_string_known_string_size(buffer, timestamp->seconds, (uint8_t)seconds_size);
  buffer += seconds_size;
  *buffer = '.';
  buffer++;
  // nanos is in range, so nanos_size is 1..9 and this can no longer go negative
  size_t zeroPadCount = 9 - nanos_size;
  if (zeroPadCount) {
    memset(buffer, '0', zeroPadCount);
    buffer += zeroPadCount;
  }
  arnm_int64_to_string_known_string_size(buffer, timestamp->nanos, (uint8_t)nanos_size);
  return result_size;
}
