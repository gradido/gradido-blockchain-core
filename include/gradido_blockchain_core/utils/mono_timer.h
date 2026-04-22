#ifndef GRADIDO_BLOCKCHAIN_CORE_UTILS_MONO_TIMER_H
#define GRADIDO_BLOCKCHAIN_CORE_UTILS_MONO_TIMER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t start;
} grbn_mono_timer;

//! need to be called at start if used on windows with multiple threads
//! \return false if on Windows Query Performance Frequency init failed
bool grbn_mono_timer_init();

//! use also for first init
void grbn_mono_timer_reset(grbn_mono_timer* p);

double grbn_mono_timer_seconds(const grbn_mono_timer* p);
double grbn_mono_timer_millis(const grbn_mono_timer* p);
double grbn_mono_timer_micros(const grbn_mono_timer* p);
uint64_t grbn_mono_timer_nanos(const grbn_mono_timer* p);

//! will write to buffer only if enough space
//! buffer need to be at least buffer_size (inclusive null terminator)
//! \return how many character would be written to buffer - inclusive null terminator, if buffer was large enough
int grbn_mono_timer_string(char* buffer, size_t buffer_size, const grbn_mono_timer* p);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_UTILS_MONO_TIMER_H