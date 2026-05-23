#define R128_IMPLEMENTATION
#include "../third_party/r128/r128.h"
#include <stdio.h>

static const uint64_t DECAY_FACTOR_PER_SECOND =   18446743668527564940ULL;

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
        r128Mul(&base, &base, &base); // base *= base
    }
    printf("};\n");
    return 0;
}

#endif
