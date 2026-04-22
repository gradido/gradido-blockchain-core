#include "gradido_blockchain_core/data/unit.h"

#define R128_IMPLEMENTATION
#include "r128/r128.h"

#include <ctype.h>
#include <math.h>
#include <memory.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const grdd_duration_seconds SECONDS_PER_YEAR = 31556952; // seconds in a year in gregorian calender
static const grdd_timestamp_seconds DECAY_START_TIME = 1620927991;
// precalculated decay factor for deterministic decay calculation across platforms, 2^64 / SECONDS_PER_YEAR
//static const uint64_t DECAY_FACTOR_PER_SECOND = 18446743668527564800ULL;
static const uint64_t DECAY_FACTOR_PER_SECOND =   18446743668527564953ULL;
// precalculated powers of 10 for fast rounding
static const uint64_t POW10[] = { 
	1, 
	10, 
	100, 
	1000, 
	10000, 
	100000, 
	1000000, 
	10000000, 
	100000000,
	1000000000,
	10000000000,
	100000000000,
	1000000000000,
	10000000000000,
	100000000000000,
	1000000000000000,
	10000000000000000,
	100000000000000000,
	1000000000000000000,
	10000000000000000000ULL
 };

void grdd_unit_round(grdd_unit* result, grdd_unit* value, uint8_t precision)
{	
	if (precision > 19) {
		r128Copy(result, value);
		return;
	}
	if (precision == 0) {
		r128Round(result, value);
		return;
	}
	R128 scale_factor;
	r128FromInt(&scale_factor, POW10[precision]);
	R128 scaled_value;
	r128Mul(&scaled_value, value, &scale_factor);
	R128 rounded_scaled_value;
	r128Round(&rounded_scaled_value, &scaled_value);
	r128Div(result, &rounded_scaled_value, &scale_factor);
}


int grdd_unit_to_string(char* buffer, size_t buffer_size, grdd_unit* value, uint8_t precision)
{
	if (precision > 19) precision = 19;

	R128ToStringFormat options = {
		.sign = R128ToStringSign_Default,
		.width = 0,
		.precision = precision,
		.zeroPad = 0,
		.decimal = precision > 0,
		.leftAlign = 0 // right align
	};
	return r128ToStringOpt(buffer, buffer_size, value, &options);	
}

grdd_timestamp_seconds grdd_unit_decay_start_time()
{
	return DECAY_START_TIME;
}

void grdd_unit_calculate_decay(grdd_unit* result, grdd_unit* value, grdd_duration_seconds duration)
{
  if (duration == 0) {
		r128Copy(result, value);
		return;
	}
	
	// decay for one year is 50%
	/*
	* while (seconds >= SECONDS_PER_YEAR) {
		mGradidoCent *= 0.5;
		seconds -= SECONDS_PER_YEAR;
	}
	*/
	grdd_unit gradido_cent;
	
	// optimizing with bit shift for whole years
	if (duration >= SECONDS_PER_YEAR) {
		uint64_t times = (uint64_t)(duration / SECONDS_PER_YEAR);
		if (times > 63) {
				// after more than 63 years, all gradidos are decayed
				r128FromInt(result, 0);
				return;
		}
		duration = duration - times * SECONDS_PER_YEAR;
		r128Shr(&gradido_cent, value, times);
		if (!duration) {
			r128Copy(result, &gradido_cent);
			return;
		}
	} else {
		r128Copy(&gradido_cent, value);
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
	//return (grdd_unit)((double)gradidoCent * pow(2.0, (double)-duration / (double)SECONDS_PER_YEAR));

	R128 factor = { .lo = 0, .hi = 1 };
	R128 base = { .lo = DECAY_FACTOR_PER_SECOND, .hi = 0 };
	uint64_t exp = duration;

	while (exp > 0) {
			if ((exp & 1) == 1) {
					r128Mul(&factor, &factor, &base);  // factor *= base
			}
			r128Mul(&base, &base, &base);        // base *= base  
			exp >>= 1;
	}
	
	// Final: balance * factor
	r128Mul(result, &gradido_cent, &factor);
}

bool grdd_unit_calculate_duration_seconds(grdd_timestamp_seconds startTime, grdd_timestamp_seconds endTime, grdd_duration_seconds* outSeconds)
{
	if (!outSeconds) {
		return false;
	}
  if(startTime > endTime) {
		return false;
	}
	grdd_timestamp_seconds start = startTime >  DECAY_START_TIME ? startTime : DECAY_START_TIME;
	grdd_timestamp_seconds end = endTime > DECAY_START_TIME ? endTime : DECAY_START_TIME;
  if (outSeconds) {
    if (start == end) {
      *outSeconds = 0;
    } else {
      *outSeconds = end - start;
    }
  }
	return true;
}
