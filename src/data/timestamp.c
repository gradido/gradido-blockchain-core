#include "include/gradido_blockchain/data/timestamp.h"

grdd_timestamp grdd_timestamp_minus(const grdd_timestamp* t1, grdd_timestamp* t2) 
{
  int32_t nanos = t1->nanos - t2->nanos;
  if (nanos < 0) {
    return (grdd_timestamp){.seconds = t1->seconds - t2->seconds - 1, .nanos = nanos + 1000000000};
  }
  return (grdd_timestamp){.seconds = t1->seconds - t2->seconds, .nanos = nanos};
}

grdd_timestamp grdd_timestamp_plus(const grdd_timestamp* t1, grdd_timestamp* t2) 
{
  int32_t nanos = t1->nanos + t2->nanos;
  if (nanos >= 1000000000) {
    return (grdd_timestamp){.seconds = t1->seconds + t2->seconds + 1, .nanos = nanos - 1000000000};
  }
  return (grdd_timestamp){.seconds = t1->seconds + t2->seconds, .nanos = nanos};
}

grdd_timestamp grdd_timestamp_from_timestamp_seconds(const grdd_timestamp_seconds* timestamp_seconds) 
{
  return (grdd_timestamp){.seconds = timestamp_seconds->seconds, .nanos = 0};
}