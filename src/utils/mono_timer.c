#include "gradido_blockchain_core/utils/converter.h"
#include "gradido_blockchain_core/utils/mono_timer.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>

// counts per second
static LARGE_INTEGER freq = { .QuadPart = 0 };

// for support more platforms, look into this as example: 
// https://github.com/siu/minunit/blob/master/minunit.h
static uint64_t get_time_ns() 
{    
    
    if (freq.QuadPart == 0) {
        grbn_mono_timer_init();
    }

    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter)) {
        fprintf(stderr, "Error: QueryPerformanceCounter failed\n");
        exit(1);
    }

    return ((uint64_t)counter.QuadPart * 1000000000ULL) / (uint64_t)freq.QuadPart;
}

#else
#include <time.h>

static uint64_t get_time_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

#endif

bool grbn_mono_timer_init()
{
#ifdef _WIN32
    if (!QueryPerformanceFrequency(&freq)) {
        fprintf(stderr, "Error: QueryPerformanceFrequency failed\n");
        return false;
    }
#endif
    return true;
}

void grbn_mono_timer_reset(grbn_mono_timer* p) {
    p->start = get_time_ns();
}

uint64_t grbn_mono_timer_nanos(const grbn_mono_timer* p) {
    uint64_t current = get_time_ns();
    return current - p->start;
}

double grbn_mono_timer_micros(const grbn_mono_timer* p) {
    return grbn_mono_timer_nanos(p) / 1e3;
}

double grbn_mono_timer_millis(const grbn_mono_timer* p) {
    return grbn_mono_timer_nanos(p) / 1e6;
}

double grbn_mono_timer_seconds(const grbn_mono_timer* p) {
    return grbn_mono_timer_nanos(p) / 1e9;
}

int grbn_mono_timer_string(char* buffer, size_t buffer_size, const grbn_mono_timer* p)
{
    uint64_t ns = grbn_mono_timer_nanos(p);
    if (ns < 1000) {
        size_t duration_string_size = grdu_uint64_to_string_size(ns);
        if (buffer_size < duration_string_size + 4) {
            return duration_string_size + 4;
        }
        const char* suffix = " ns";
        grdu_uint64_to_string(ns, buffer);
        memcpy(buffer + duration_string_size, suffix, 3);
        return duration_string_size + 4;
    }
    if (ns < 1000000) {
        return snprintf(buffer, buffer_size, "%.4f us", ns / 1000.0) + 1;
    }
    if (ns < 1000000000) {
        return snprintf(buffer, buffer_size, "%.4f ms", ns / 1000000.0) + 1;
    }
    return snprintf(buffer, buffer_size, "%.4f s", ns / 1000000000.0) + 1;
}