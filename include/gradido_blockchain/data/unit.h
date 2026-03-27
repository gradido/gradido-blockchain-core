#ifndef GRADIDO_BLOCKCHAIN_C_LOGIC_UNIT_H
#define GRADIDO_BLOCKCHAIN_C_LOGIC_UNIT_H

#include <stdbool.h> 
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct grdd_timestamp_seconds;
struct grdd_duration_seconds;

typedef struct grdd_unit 
{
  int64_t gradidoCent;
} grdd_unit;

grdd_unit grdd_unit_from_decimal(double gdd);
grdd_unit grdd_unit_from_string(const char* gdd_string);

//! \return 0 for ok, 1 for invalid precision, 2 for printf encoding error
//          and -x if string buffer wasn't big enough where x is the number of missing bytes
int grdd_unit_to_string(const grdd_unit* u, char* buffer, size_t bufferSize, uint8_t precision);

inline grdd_unit grdd_unit_negated(const grdd_unit* u) 
{
  grdd_unit gradidoUnit = {u->gradidoCent};  
  gradidoUnit.gradidoCent *= -1;
  return gradidoUnit;
}

inline void grdd_unit_negate(grdd_unit* u)
{
    if (u) u->gradidoCent = -u->gradidoCent;
}

static inline grdd_unit grdd_unit_zero()
{
  return (grdd_unit){0};
}
//! return false if startTime > endTime
bool grdd_unit_calculate_duration_seconds(
  const grdd_timestamp_seconds* startTime, 
  const grdd_timestamp_seconds* endTime,
  grdd_duration_seconds* outSeconds
);

grdd_unit grdd_unit_calculate_decay(const grdd_unit* u, grdd_duration_seconds* duration);

grdd_unit grdd_unit_calculate_decay_timestamp(
  const grdd_unit* u, 
  const grdd_timestamp_seconds* startTime, 
  const grdd_timestamp_seconds* endTime
);

grdd_unit grdd_unit_calculate_compound_interest(const grdd_unit* u, grdd_duration_seconds* duration);

grdd_unit grdd_unit_calculate_compound_interest_timestamp(
  const grdd_unit* u, 
  const grdd_timestamp_seconds* startTime, 
  const grdd_timestamp_seconds* endTime
);

#ifdef __cplusplus
}
#endif


#endif // GRADIDO_BLOCKCHAIN_C_LOGIC_UNIT_H