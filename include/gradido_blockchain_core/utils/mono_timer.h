#ifndef GRADIDO_BLOCKCHAIN_CORE_UTILS_MONO_TIMER_H
#define GRADIDO_BLOCKCHAIN_CORE_UTILS_MONO_TIMER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t grdu_mono_timer;

//! need to be called at start if used on windows with multiple threads
//! \return false if on Windows Query Performance Frequency init failed
bool grdu_mono_timer_init();
//! use also for first init
void grdu_mono_timer_reset(grdu_mono_timer* start);

double grdu_mono_timer_seconds(grdu_mono_timer* start);
double grdu_mono_timer_millis(grdu_mono_timer* start);
double grdu_mono_timer_micros(grdu_mono_timer* start);
int64_t grdu_mono_timer_nanos(grdu_mono_timer* start);

//! set start to current time and write duration since start to buffer
//! will write to buffer only if enough space
//! buffer need to be at least buffer_size (inclusive null terminator)
//! \return how many character would be written to buffer - not counting the terminating null character, if buffer was large enough
int grdu_mono_timer_string(char* buffer, size_t buffer_size, grdu_mono_timer* start);


#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_UTILS_MONO_TIMER_H