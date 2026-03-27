#include "include/gradido_blockchain/data/duration_seconds.h"
#include "include/gradido_blockchain/data/timestamp_seconds.h"
#include "include/gradido_blockchain/data/unit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

const double SECONDS_PER_YEAR = 31556952.0; // seconds in a year in gregorian calender
const grdd_timestamp DECAY_START_TIME = (grdd_timestamp){.seconds = 1620927991, .nanos = 0};

double roundToPrecision(double gdd, uint8_t precision) 
{
  double factor = pow(10.0, precision);
	return round(gdd * factor) / factor;
}

grdd_unit grdd_unit_from_decimal(double gdd)
{
  grdd_unit gradidoUnit = {
    (int64_t)(roundToPrecision(gdd, 4) * 10000.0)
  };
  return gradidoUnit;
}

grdd_unit grdd_unit_from_string(const char* gdd_string)
{
  if (!gdd_string) return (grdd_unit){0};
  char* end;
  double gdd_double = strtod(gdd_string, &end);
  if (end == gdd_string || *end != '\0') {
    // invalid string 
    return (grdd_unit){0};
  }
  return grdd_unit_from_decimal(gdd_double);
}

int grdd_unit_to_string(const grdd_unit* u, char* buffer, size_t bufferSize, uint8_t precision)
{
  if (precision > 4) return 1; // C hat keine Exceptions

  // Convert to double
  double decimal = (double)(u->gradidoCent) / 10000.0;

  // Round down like Node.js if precision < 4
  if (precision < 4) {
      double factor = pow(10.0, precision);
      decimal = round(decimal * factor) / factor;
  }

  // Write to buffer
  int written = snprintf(buffer, bufferSize, "%.*f", precision, decimal);

  // snprintf returns number of chars that would have been written (excluding null)
  // snprintf return negative value on encoding error
  if (written < 0) return 2;
  if ((size_t)written < bufferSize) {
    return 0;
  }
  return bufferSize - written;
}

grdd_unit grdd_unit_calculate_decay(const grdd_unit* u, grdd_duration_seconds* duration)
{
  if (duration->seconds == 0) return (grdd_unit){u->gradidoCent};
	
	// decay for one year is 50%
	/*
	* while (seconds >= SECONDS_PER_YEAR) {
		mGradidoCent *= 0.5;
		seconds -= SECONDS_PER_YEAR;
	}
	*/
	int64_t gradidoCent = u->gradidoCent;
	// optimize version from above
	if (duration->seconds >= SECONDS_PER_YEAR) {
		uint64_t times = (uint64_t)(duration->seconds / SECONDS_PER_YEAR);
		duration->seconds = duration->seconds - times * SECONDS_PER_YEAR;
		gradidoCent = u->gradidoCent >> times;
		if (!duration->seconds) return (grdd_unit){gradidoCent};
	}

	/*!
	 *  calculate decay factor with compound interest formula converted to q <br>
	 *  n = (lg Kn - lg K0) / lg q => <br>
	 *  lg q = (lg Kn - lg K0) / n => <br>
	 *  q = e^((lg Kn - lg K0) / n)   <br>
	 * <br>
	 * with:
	 * <ul>
	 *  <li>q = decay_factor</li>
	 *  <li>n = days_per_year * 60 * 60 * 24 = seconds per year</li>
	 *  <li>Kn = 50 (capital after a year)</li>
	 *  <li>K0 = 100 (capital at start)</li>
	 * </ul>
	 * further simplified:
	 * lg 50 - lg 100 = lg 2 =>
	 * q = e^(lg 2 / n) = 2^(x/n)
	 * with x as seconds in which decay occured
	 */
	// https://www.wolframalpha.com/input?i=%28e%5E%28lg%282%29+%2F+31556952%29%29%5Ex&assumption=%7B%22FunClash%22%2C+%22lg%22%7D+-%3E+%7B%22Log%22%7D
	// from wolframalpha, based on the interest rate formula
	return (grdd_unit){((int64_t)((double)(gradidoCent) * pow(2.0, (double)((double)(-duration->seconds) / SECONDS_PER_YEAR))))};
}

grdd_unit grdd_unit_calculate_compound_interest(const grdd_unit* u, grdd_duration_seconds* duration) {
  return grdd_unit_calculate_decay(u, duration);
}

grdd_unit grdd_unit_calculate_compound_interest_timestamp(
  const grdd_unit* u, 
  const grdd_timestamp_seconds* startTime, 
  const grdd_timestamp_seconds* endTime
) {
  grdd_duration_seconds duration;
  if(!grdd_unit_calculate_duration_seconds(startTime, endTime, &duration)) {
    return (grdd_unit){0};
  }
  return grdd_unit_calculate_compound_interest(u, &duration);
}


grdd_unit grdd_unit_calculate_decay_timestamp(
  const grdd_unit* u, 
  const grdd_timestamp_seconds* startTime, 
  const grdd_timestamp_seconds* endTime
) {
  grdd_duration_seconds duration;
  if(!grdd_unit_calculate_duration_seconds(startTime, endTime, &duration)) {
    return (grdd_unit){0};
  }
  return grdd_unit_calculate_decay(u, &duration);
}

bool grdd_unit_calculate_duration_seconds(
  const grdd_timestamp* startTime, 
  const grdd_timestamp* endTime,
  grdd_duration_seconds* outDuration
) {
	if (!outDuration) {
		return false;
	}
  if(grdd_timestamp_gt(startTime, endTime)) {
		return false;
	}
	grdd_timestamp start = grdd_timestamp_gt(startTime, &DECAY_START_TIME) ? *startTime : DECAY_START_TIME;
	grdd_timestamp end = grdd_timestamp_gt(endTime, &DECAY_START_TIME) ? *endTime : DECAY_START_TIME;
	if (grdd_timestamp_eq(&start, &end)) {
		*outDuration = (grdd_duration_seconds){0};
		return true;
	}
	*outDuration = grdd_timestamp_sub(&end, &start);
	return true;
}
