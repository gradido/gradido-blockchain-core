#include "gradido_blockchain_core/utils/duration.h"

#include "r128/r128.h"

#include <string.h>
#include <stdio.h>

int grdu_duration_string(char* buffer, size_t buffer_size, grdu_duration duration, uint8_t precision)
{
    uint64_t ns = (uint64_t)duration;

    const char* suffixes[] =  { " ns"," us"," ms", " s", " m", " h", " d" };
    const uint64_t divisors[] = { 1,    1e3, 1e6,   1e9,
      60 * 1e9, // minutes
      60 * 60 * 1e9, // hours
      24 * 60 * 60 * 1e9 // days
    };
    uint8_t unitIndex = 6; // default to days

    for (int i = 0; i < 6; ++i) {
        if (ns < divisors[i+1]) {
            unitIndex = i;
            break;
        }
    }
    R128 timeDiff;
    r128FromInt(&timeDiff, ns);
    if (divisors[unitIndex] > 1) {
        R128 divider = { .lo = 0, .hi = 1 };
        r128FromInt(&divider, divisors[unitIndex]);
        r128Div(&timeDiff, &timeDiff, &divider);
    }
    R128ToStringFormat opt = {
      .sign = R128ToStringSign_Default,
      .width = precision + 2,
      .precision = precision,
      .zeroPad = 0,
      .decimal = precision > 0,
      .leftAlign = 0,
    };
    size_t written = r128ToStringOpt(buffer, buffer_size, &timeDiff, &opt);
    const char* suffix = suffixes[unitIndex];
    size_t suffix_length = unitIndex > 2 ? 3 : 4; // first 3 suffixes have length 3, others have length 2
    if (buffer_size < written + suffix_length + 1) {
        return written + suffix_length;
    }
    memcpy(buffer + written, suffix, suffix_length);
    return written + suffix_length;
}

int grdu_duration_string_simple(char* buffer, size_t buffer_size, grdu_duration duration)
{
    uint64_t ns = (uint64_t)duration;
    if (ns < 1000ULL) {
        return snprintf(buffer, buffer_size, "%llu ns", (unsigned long long)ns);
    }
    else if (ns < 1000000ULL) {
        return snprintf(buffer, buffer_size, "%.3f us", ns / 1000.0);
    }
    else if (ns < 1000000000ULL) {
        return snprintf(buffer, buffer_size, "%.3f ms", ns / 1000000.0);
    }
    else if (ns < 60ULL * 1000000000ULL) {
        return snprintf(buffer, buffer_size, "%.3f s", ns / 1000000000.0);
    }
    else if (ns < 60ULL * 60ULL * 1000000000ULL) {
        return snprintf(buffer, buffer_size, "%.3f m", ns / (60.0 * 1000000000.0));
    }
    else if (ns < 24ULL * 60ULL * 60ULL * 1000000000ULL) {
        return snprintf(buffer, buffer_size, "%.3f h", ns / (3600.0 * 1000000000.0));
    }
    else {
        return snprintf(buffer, buffer_size, "%.3f d", ns / (86400.0 * 1000000000.0));
    }
}