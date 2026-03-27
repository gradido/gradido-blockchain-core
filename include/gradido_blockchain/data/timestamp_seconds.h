#ifndef GRADIDO_BLOCKCHAIN_C_DATA_TIMESTAMP_SECONDS_H
#define GRADIDO_BLOCKCHAIN_C_DATA_TIMESTAMP_SECONDS_H

#include <stdbool.h> 
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct grdd_timestamp;

typedef struct grdd_timestamp_seconds 
{
  int64_t seconds;
} grdd_timestamp_seconds;

static inline bool grdd_timestamp_seconds_empty(const grdd_timestamp_seconds* timestamp) {
  return !timestamp->seconds;
}
static inline bool grdd_timestamp_seconds_eq(const grdd_timestamp_seconds* t1, grdd_timestamp_seconds* t2) {
  return t1->seconds == t2->seconds;
}
static inline bool grdd_timestamp_seconds_gt(const grdd_timestamp_seconds* t1, const grdd_timestamp_seconds* t2) {
  return t1->seconds > t2->seconds;
}
static inline bool grdd_timestamp_seconds_lt(const grdd_timestamp_seconds* t1, const grdd_timestamp_seconds* t2) {
  return t1->seconds < t2->seconds;
}
static inline grdd_timestamp_seconds grdd_timestamp_seconds_sub(const grdd_timestamp_seconds* t1, grdd_timestamp_seconds* t2) {
  return (grdd_timestamp_seconds){.seconds = t1->seconds - t2->seconds};
}
static inline grdd_timestamp_seconds grdd_timestamp_seconds_add(const grdd_timestamp_seconds* t1, grdd_timestamp_seconds* t2) {
  return (grdd_timestamp_seconds){.seconds = t1->seconds + t2->seconds};
}

grdd_timestamp_seconds grdd_timestamp_seconds_from_timestamp(const grdd_timestamp* timestamp);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_C_DATA_TIMESTAMP_SECONDS_H