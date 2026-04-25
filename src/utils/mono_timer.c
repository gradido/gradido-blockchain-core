#include "gradido_blockchain_core/utils/mono_timer.h"
#include "gradido_blockchain_core/utils/duration.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>

// counts per second
static LARGE_INTEGER freq = { .QuadPart = 0 };

// for support more platforms, look into this as example:
// https://github.com/siu/minunit/blob/master/minunit.h
static int64_t get_time_ns()
{
    if (freq.QuadPart == 0) {
        grdu_mono_timer_init();
    }

    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter)) {
        fprintf(stderr, "Error: QueryPerformanceCounter failed\n");
        exit(1);
    }

    return ((int64_t)counter.QuadPart * 1000000000LL) / (int64_t)freq.QuadPart;
}

#else
#include <time.h>

static int64_t get_time_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000LL + (int64_t)t.tv_nsec;
}

#endif

bool grdu_mono_timer_init()
{
#ifdef _WIN32
    if (!QueryPerformanceFrequency(&freq)) {
        fprintf(stderr, "Error: QueryPerformanceFrequency failed\n");
        return false;
    }
#endif
    return true;
}

void grdu_mono_timer_reset(grdu_mono_timer* start) {
    *start = get_time_ns();
}

int64_t grdu_mono_timer_nanos(grdu_mono_timer start) {
    return get_time_ns() - start;
}

double grdu_mono_timer_micros(grdu_mono_timer start) {
    return (double)grdu_mono_timer_nanos(start) / 1e3;
}

double grdu_mono_timer_millis(grdu_mono_timer start) {
    return (double)grdu_mono_timer_nanos(start) / 1e6;
}

double grdu_mono_timer_seconds(grdu_mono_timer start) {
    return (double)grdu_mono_timer_nanos(start) / 1e9;
}
int grdu_mono_timer_string(char* buffer, size_t buffer_size, grdu_mono_timer start) {
    return grdu_duration_string(buffer, buffer_size, grdu_mono_timer_nanos(start), 4);
}