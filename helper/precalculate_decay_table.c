#define R128_IMPLEMENTATION
#include "r128/r128.h"
#include "fp256/fp256.h"
#include <stdio.h>

static const uint64_t DECAY_FACTOR_PER_SECOND =   18446743668527564940ULL;



static inline void r128Mul_precise(R128* dst, const R128* a, const R128* b)
{
    //
    // Handle sign separately
    //
    int sign = 0;

    R128 ta = *a;
    R128 tb = *b;

    if (r128IsNeg(&ta)) {
        r128__neg(&ta, &ta);
        sign ^= 1;
    }

    if (r128IsNeg(&tb)) {
        r128__neg(&tb, &tb);
        sign ^= 1;
    }

    //
    // Q64.64 multiplication:
    //
    // (ahi<<64 + alo) * (bhi<<64 + blo)
    //
    // Result after >>64:
    //
    //   (alo*blo)>>64
    // + (ahi*blo)
    // + (alo*bhi)
    //
    // High-high term contributes above Q64.64 range.
    //

    __uint128_t p0 =
        (__uint128_t)ta.lo * tb.lo;

    __uint128_t p1 =
        (__uint128_t)ta.hi * tb.lo;

    __uint128_t p2 =
        (__uint128_t)ta.lo * tb.hi;

    __uint128_t p3 =
        (__uint128_t)ta.hi * tb.hi;

    //
    // Q64.64:
    //
    // result =
    //      (p0 >> 64)
    //    + p1
    //    + p2
    //    + (p3 << 64)
    //

    __uint128_t low =
        (p0 >> 64)
        + (uint64_t)p1
        + (uint64_t)p2;

    __uint128_t high =
        (p1 >> 64)
        + (p2 >> 64)
        + p3
        + (low >> 64);

    R128 out = {
        .lo = (uint64_t)low,
        .hi = (uint64_t)high
    };
    *dst = out;
}

#ifndef R128_INTEL

int main()
{
    printf("Error, please run it on a Intel or AMD64 CPU\n");
    return 0;
}

#else

int main()
{
    //printf("Precalculating decay table...\n");
    printf("static const uint64_t DECAY_POWERS[] = {\n");
    R128 base = { .lo = DECAY_FACTOR_PER_SECOND, .hi = 0 };
    for (int i = 0; i < 32; i++) {
        printf("    %lluULL,\n", base.lo);
        r128Mul_precise(&base, &base, &base); // base *= base
    }
    printf("};\n");

    // Reverse table
    printf("static const uint64_t DECAY_POWERS_INV[] = {\n");

    base.lo = DECAY_FACTOR_PER_SECOND;
    base.hi = 0;

    for (int i = 0; i < 32; i++) {
        R128 inv;
        // inv = 1 / base
        r128Div(&inv, &R128_one, &base);
        printf("    %lluULL,\n", inv.lo);
        // next power
        r128Mul_precise(&base, &base, &base);
    }

    printf("};\n");
    return 0;
}

#endif
