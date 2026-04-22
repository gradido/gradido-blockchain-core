#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_UNIT_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_UNIT_H

#include "types.h"

#include "r128/r128.h"

#include <stdbool.h> 
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef R128 grdd_unit;

 /**
 * @brief Rounds a fixed-point grdd_unit value to a specified decimal precision.
 *
 * @param[out] result
 *   Pointer to store the rounded result. Must not be NULL.
 * 
 * @param[in]  value
 *   The input value in fixed-point representation Q64,64
 * 
 * @param[in]  precision
 *   Target number of decimal places. *
 */
void grdd_unit_round(grdd_unit* result, grdd_unit* value, uint8_t precision);

inline void grdd_unit_from_decimal(grdd_unit* result, double gdd) {
  r128FromFloat(result, gdd);
}
inline double grdd_unit_to_decimal(grdd_unit* value) {
  return r128ToFloat(value);
}

inline void grdd_unit_from_string(grdd_unit* result, const char* gdd_string, char **endptr) {
  r128FromString(result, gdd_string, endptr);
}

// flip sign
inline void grdd_unit_negate(grdd_unit* result, grdd_unit* value) {
  r128Neg(result, value);
}

//! \return 0 for equal, 1 = a > b and -1 = a < b
inline int grdd_unit_compare(grdd_unit* a, grdd_unit* b) {
  return r128Cmp(a, b);
}

grdd_timestamp_seconds grdd_unit_decay_start_time();

//! return false if startTime > endTime
//! make sure that returned duration starts after decay start time, returns 0 if time range is entirely before decay start time
bool grdd_unit_calculate_duration_seconds(grdd_timestamp_seconds startTime, grdd_timestamp_seconds endTime, grdd_duration_seconds* outSeconds);

/**
 * @brief Converts a fixed-point grdd_unit value to its string representation.
 *
 * This function formats a grdd_unit value (scaled by 10^4) into a human-readable
 * decimal string with the specified precision (0–4 fractional digits).
 *
 * The value is first rounded using HALF-UP rounding to the requested precision.
 * If the requested precision is greater than 19, it is clamped to 19.
 *
 * The resulting string:
 * - Includes a leading '-' sign for negative values
 * - Uses a '.' as decimal separator if precision > 0
 * - Omits the fractional part entirely if precision == 0
 *
 * The conversion is performed using integer arithmetic only, ensuring
 * deterministic and platform-independent results.
 *
 * @param[in]  u
 *   The input value in fixed-point representation (scaled by 10^4).
 *
 * @param[out] buffer
 *   Destination buffer to store the resulting null-terminated string.
 *   The caller must ensure that the buffer is large enough. Will write at most 42 bytes (including NUL) to dst.
 *
 * @param[in]  precision
 *   Number of digits after the decimal point (0–19). Values greater than 19
 *   are clamped to 19.
 *
 * @return
 *   >= 0 - number of characters written (excluding null terminator)
 *   -1   - rounding failed (e.g. due to overflow)
 */
int grdd_unit_to_string(char* buffer, size_t buffer_size, grdd_unit* value, uint8_t precision);

void grdd_unit_calculate_decay(grdd_unit* result, grdd_unit* value, grdd_duration_seconds duration);

inline void grdd_unit_add(grdd_unit* result, grdd_unit* a, grdd_unit* b) {
  r128Add(result, a, b);
}

inline void grdd_unit_subtract(grdd_unit* result, grdd_unit* a, grdd_unit* b) {
  r128Sub(result, a, b);
}

inline void grdd_unit_multiplicate(grdd_unit* result, grdd_unit* a, grdd_unit* b) {
  r128Mul(result, a, b);
}

#ifdef __cplusplus
}
#endif


#endif // GRADIDO_BLOCKCHAIN_CORE_DATA_UNIT_H