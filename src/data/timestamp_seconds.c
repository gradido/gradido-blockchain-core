#include "include/gradido_blockchain/data/timestamp.h"
#include "include/gradido_blockchain/data/timestamp_seconds.h"

grdd_timestamp_seconds grdd_timestamp_seconds_from_timestamp(const grdd_timestamp* timestamp)
{
  return (grdd_timestamp_seconds){.seconds = timestamp->seconds};
}