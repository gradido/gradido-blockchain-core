#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_TIMESTAMP_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_TIMESTAMP_H

#include "types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grdd_timestamp {
  int64_t seconds;
  int32_t nanos;
} grdd_timestamp;

static inline bool grdd_timestamp_empty(const grdd_timestamp *timestamp) {
  return !timestamp->seconds && !timestamp->nanos;
}

static inline bool grdd_timestamp_eq(const grdd_timestamp *t1, grdd_timestamp *t2) {
  return t1->seconds == t2->seconds && t1->nanos == t2->nanos;
}

static inline bool grdd_timestamp_gt(const grdd_timestamp *t1, const grdd_timestamp *t2) {
  return t1->seconds > t2->seconds || (t1->seconds == t2->seconds && t1->nanos > t2->nanos);
}

static inline bool grdd_timestamp_lt(const grdd_timestamp *t1, const grdd_timestamp *t2) {
  return t1->seconds < t2->seconds || (t1->seconds == t2->seconds && t1->nanos < t2->nanos);
}

grdd_timestamp grdd_timestamp_minus(const grdd_timestamp *t1, const grdd_timestamp *t2);
grdd_timestamp grdd_timestamp_plus(const grdd_timestamp *t1, const grdd_timestamp *t2);

static inline grdd_timestamp grdd_timestamp_from_seconds(int64_t seconds) {
  grdd_timestamp timestamp;
  timestamp.seconds = seconds;
  timestamp.nanos = 0;
  return timestamp;
}

grdd_timestamp grdd_timestamp_from_timestamp_seconds(
    const grdd_timestamp_seconds timestamp_seconds
);
int64_t grdd_timestamp_get_seconds(const grdd_timestamp *timestamp);
int32_t grdd_timestamp_get_nanos(const grdd_timestamp *timestamp);
size_t grdd_timestamp_calculate_string_size(const grdd_timestamp *timestamp);
/**
 * @brief Write the value as text into a buffer the caller sized.
 *
 * @p buffer_size counts the terminator, the way snprintf counts it and the way
 * arnm's converters do: a buffer of exactly the character count is one byte short and is
 * refused rather than filled. The matching _calculate_string_size() returns that character
 * count, so a caller sizing a buffer from it adds one.
 *
 * @param[out] buffer      Destination; not NULL.
 * @param[in]  buffer_size Bytes available, terminator included.
 * @return Characters written, terminator not counted. When the buffer is too small nothing is
 *         written and the same figure comes back, so the caller learns what to allocate --
 *         which means a return equal to @p buffer_size or larger says the call did nothing.
 */
size_t grdd_timestamp_to_string(char *buffer, size_t buffer_size, const grdd_timestamp *timestamp);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_DATA_TIMESTAMP_H
