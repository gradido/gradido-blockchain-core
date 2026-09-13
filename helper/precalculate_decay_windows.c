// Prints the DECAY_WINDOW_FACTORS table used by grdd_unit_calculate_decay_windowed().
//
// The windowed decay groups the 25 bits of the sub-year remainder into five 5 bit windows and
// multiplies one factor per window instead of one per bit. The factors are derived from the
// very same DECAY_POWERS as the bit chain, so no new constants and no floating point enter the
// system: DECAY_WINDOW_FACTORS[j][c] is the factor of c * 2^(5*j) seconds.
//
//   gcc -O2 -std=c11 helper/precalculate_decay_windows.c -o precalc_windows && ./precalc_windows
//
// The gradido-blockchain-zk crate builds the same table in Rust (decay::window_table) and both are
// checked against each other by the test vectors in its test/decay_vectors.csv.

#include <stdint.h>
#include <stdio.h>

#define WINDOW_BITS 5
#define WINDOWS 5
#define WINDOW_SIZE (1 << WINDOW_BITS)

// same table as in unit.c, one factor per bit of the remainder
static const uint64_t DECAY_POWERS[25] = {
    18446743668527564940ULL, 18446743263345587163ULL, 18446742452981658309ULL,
    18446740832253907398ULL, 18446737590798832767ULL, 18446731107890392267ULL,
    18446718142080346313ULL, 18446692210487594564ULL, 18446640347411451525ULL,
    18446536621696605848ULL, 18446329172016664620ULL, 18445914279655690837ULL,
    18445084522928643346ULL, 18443425121448271984ULL, 18440106766335414243ULL,
    18433471847125226169ULL, 18420209169759874097ULL, 18393712435208807257ULL,
    18340833254760868098ULL, 18235530516106764452ULL, 18026735334708307356ULL,
    17616289656815978922ULL, 16823221487369777986ULL, 15342587292489394070ULL,
    12760787697116905635ULL,
};

// high 64 bits of a * b, without a 128 bit integer type (MSVC has none)
static uint64_t mul_high(uint64_t a, uint64_t b) {
    uint64_t a_lo = (uint32_t)a, a_hi = a >> 32;
    uint64_t b_lo = (uint32_t)b, b_hi = b >> 32;
    uint64_t lo_lo = a_lo * b_lo;
    uint64_t hi_lo = a_hi * b_lo;
    uint64_t lo_hi = a_lo * b_hi;
    uint64_t hi_hi = a_hi * b_hi;
    uint64_t cross = (lo_lo >> 32) + (uint32_t)hi_lo + lo_hi;
    return hi_hi + (hi_lo >> 32) + (cross >> 32);
}

// floor(a * b / 2^64) in Q64.64, where a may be exactly 2^64 (represented as hi = 1)
static void mul_q64(uint64_t *hi, uint64_t *lo, uint64_t b) {
    if (*hi == 1 && *lo == 0) {
        *lo = b;
        *hi = 0;
        return;
    }
    *lo = mul_high(*lo, b);
    *hi = 0;
}

int main(void) {
    printf("static const uint64_t DECAY_WINDOW_FACTORS[%d][%d] = {\n", WINDOWS, WINDOW_SIZE);
    for (int j = 0; j < WINDOWS; ++j) {
        printf("    {\n        0ULL,");
        int column = 13;
        for (int c = 1; c < WINDOW_SIZE; ++c) {
            uint64_t hi = 1, lo = 0; // 1.0 in Q64.64
            for (int bit = 0; bit < WINDOW_BITS; ++bit) {
                if ((c >> bit) & 1) { mul_q64(&hi, &lo, DECAY_POWERS[bit + j * WINDOW_BITS]); }
            }
            if (column > 80) {
                printf("\n       ");
                column = 7;
            }
            column += printf(" %lluULL,", (unsigned long long)lo);
        }
        printf("\n    },\n");
    }
    printf("};\n");
    return 0;
}
