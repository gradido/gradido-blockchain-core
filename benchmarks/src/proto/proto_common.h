#ifndef GRADIDO_BLOCKCHAIN_CORE_BENCH_PROTO_COMMON_H
#define GRADIDO_BLOCKCHAIN_CORE_BENCH_PROTO_COMMON_H

/*
 * Shared pieces of the transaction index prototypes: the key size, the three hash modes the map
 * benchmark compares, a 64 bit popcount and a byte counting malloc wrapper for the two libraries
 * that bring their own allocation (stb_ds and CRoaring).
 *
 * Prototype code. It lives under benchmarks/ so that nothing here is mistaken for library API
 * before the numbers have decided what the library gets.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Public keys are Ed25519, 32 bytes, and are the map key as they are. */
#define PROTO_KEY_SIZE 32

/**
 * How a 32 byte key becomes a 64 bit hash.
 *
 * The keys are uniformly distributed when they are real Ed25519 keys, which tempts to take the
 * first eight bytes as they are. Two things argue against it, and the modes exist to measure
 * what each defence costs:
 *   - a key generator can grind the low bits of a key cheaply (2^k attempts for k equal bits),
 *     which collides the home slot of a raw hash -- PROTO_HASH_MIX8 answers that with a per
 *     instance seed and a finalizer;
 *   - a transfer recipient is any 32 bytes the sender writes down, not a key anyone had to
 *     generate, so the first eight bytes can be identical across arbitrarily many keys -- only
 *     PROTO_HASH_MIX32, which reads all four words, survives that.
 */
typedef enum proto_hash_mode {
  PROTO_HASH_RAW8 = 0,  /**< first 8 bytes as they are, no seed */
  PROTO_HASH_MIX8 = 1,  /**< first 8 bytes, seeded, splitmix64 finalizer */
  PROTO_HASH_MIX32 = 2, /**< all 32 bytes, seeded, multiply-rotate per word plus finalizer */
  PROTO_HASH_SIP13 = 3, /**< SipHash-1-3 over all 32 bytes, keyed with (seed, ~seed) */
  PROTO_HASH_MODE_COUNT
} proto_hash_mode;

static inline const char *proto_hash_mode_name(proto_hash_mode mode) {
  switch (mode) {
  case PROTO_HASH_RAW8:
    return "raw8";
  case PROTO_HASH_MIX8:
    return "mix8";
  case PROTO_HASH_MIX32:
    return "mix32";
  default:
    return "?";
  }
}

/** Native byte order read; the benchmark targets are all little endian. */
static inline uint64_t proto_load64(const uint8_t *bytes) {
  uint64_t value;
  memcpy(&value, bytes, sizeof(value));
  return value;
}

/** splitmix64 finalizer: every input bit reaches every output bit. */
static inline uint64_t proto_mix64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

static inline uint64_t proto_rotl64(uint64_t x, unsigned bits) {
  return (x << bits) | (x >> (64u - bits));
}

#define PROTO_SIPROUND(v0, v1, v2, v3)                                                             \
  do {                                                                                             \
    v0 += v1;                                                                                      \
    v1 = proto_rotl64(v1, 13);                                                                     \
    v1 ^= v0;                                                                                      \
    v0 = proto_rotl64(v0, 32);                                                                     \
    v2 += v3;                                                                                      \
    v3 = proto_rotl64(v3, 16);                                                                     \
    v3 ^= v2;                                                                                      \
    v0 += v3;                                                                                      \
    v3 = proto_rotl64(v3, 21);                                                                     \
    v3 ^= v0;                                                                                      \
    v2 += v1;                                                                                      \
    v1 = proto_rotl64(v1, 17);                                                                     \
    v1 ^= v2;                                                                                      \
    v2 = proto_rotl64(v2, 32);                                                                     \
  } while (0)

/** SipHash-1-3 of exactly 32 bytes: one compression round per word, three finalization rounds. */
static inline uint64_t proto_siphash13_32(const uint8_t *key, uint64_t k0, uint64_t k1) {
  uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
  uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
  uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
  uint64_t v3 = 0x7465646279746573ULL ^ k1;
  for (unsigned word = 0; word < 4; ++word) {
    uint64_t m = proto_load64(key + word * 8u);
    v3 ^= m;
    PROTO_SIPROUND(v0, v1, v2, v3);
    v0 ^= m;
  }
  uint64_t b = 32ull << 56;
  v3 ^= b;
  PROTO_SIPROUND(v0, v1, v2, v3);
  v0 ^= b;
  v2 ^= 0xff;
  PROTO_SIPROUND(v0, v1, v2, v3);
  PROTO_SIPROUND(v0, v1, v2, v3);
  PROTO_SIPROUND(v0, v1, v2, v3);
  return v0 ^ v1 ^ v2 ^ v3;
}

static inline uint64_t proto_hash_key(const uint8_t *key, uint64_t seed, proto_hash_mode mode) {
  switch (mode) {
  case PROTO_HASH_SIP13:
    return proto_siphash13_32(key, seed, ~seed);
  case PROTO_HASH_RAW8:
    return proto_load64(key);
  case PROTO_HASH_MIX8:
    return proto_mix64(proto_load64(key) ^ seed);
  default: {
    uint64_t h = seed;
    for (unsigned word = 0; word < 4; ++word) {
      h = proto_rotl64((h ^ proto_load64(key + word * 8u)) * 0x9e3779b97f4a7c15ULL, 31);
    }
    return proto_mix64(h);
  }
  }
}

static inline int proto_popcount64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcountll(x);
#else
  x = x - ((x >> 1) & 0x5555555555555555ULL);
  x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
  x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0fULL;
  return (int)((x * 0x0101010101010101ULL) >> 56);
#endif
}

/** Round up to the next power of two; 0 and 1 answer 1. Inputs above 2^31 are not expected. */
static inline uint32_t proto_next_pow2_u32(uint32_t value) {
  if (value <= 1) { return 1; }
  --value;
  value |= value >> 1;
  value |= value >> 2;
  value |= value >> 4;
  value |= value >> 8;
  value |= value >> 16;
  return value + 1;
}

/*
 * Byte counting malloc family for stb_ds and CRoaring. Neither tells its free how large the
 * block was, so every block carries a 16 byte header with its size -- the same bookkeeping arnm
 * refuses to do on the caller's behalf, which is exactly what the comparison is about. The
 * counters are process global and not thread safe; the benchmarks are single threaded.
 */
typedef struct proto_counted_stats {
  uint64_t current_bytes;  /**< requested bytes currently handed out, headers excluded */
  uint64_t peak_bytes;     /**< largest current_bytes seen since the last reset */
  uint64_t allocations;    /**< malloc, calloc and reallocs since the last reset */
  uint64_t current_blocks; /**< blocks currently handed out; each costs malloc its own header */
} proto_counted_stats;

void *proto_counted_malloc(size_t size);
void *proto_counted_calloc(size_t count, size_t size);
void *proto_counted_realloc(void *pointer, size_t size);
void proto_counted_free(void *pointer);
void *proto_counted_aligned_malloc(size_t alignment, size_t size);
void proto_counted_aligned_free(void *pointer);

proto_counted_stats proto_counted_get_stats(void);
/** Peak and allocation count start over; current bytes stay what they are. */
void proto_counted_reset_peak(void);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_BENCH_PROTO_COMMON_H
