/*
 * arnm's base64 against libsodium's, on the two shapes this project actually encodes.
 *
 * The mapping to JSON writes `body_bytes` and the encrypted memos as base64, and it draws that
 * pair from arnm rather than from libsodium so a build without sodium still has a JSON half.
 * Which is a reason to have written it, not a reason for it to be slower -- this is where that
 * gets checked rather than assumed.
 *
 * The comparison is not quite like for like, and the difference is in libsodium's favour to
 * name up front:
 *
 *   - `sodium_bin2base64()` takes a variant. ORIGINAL is the one arnm writes: the standard
 *     alphabet with padding, which is what a browser's atob() reads.
 *   - `sodium_base642bin()` accepts an `ignore` set of characters to skip. Passing NULL, as
 *     here, is the closest it comes to arnm's "whitespace is not skipped".
 *   - **libsodium's pair runs in constant time and arnm's does not**, and that is most of what
 *     the figures below measure. libsodium computes every character from branchless masks --
 *     `(LT(x, 26) & (x + 'A')) | ...` -- where arnm reads a 64 entry table one way and a 256
 *     entry table the other. So this is not a fair fight and is not meant to be one: it is the
 *     price of the property, on the shapes this project encodes.
 *   - Which makes the choice a question about the bytes. base64 here carries `body_bytes`,
 *     which is public, and memos, which are already ciphertext -- neither leaks anything
 *     through the timing of its own encoding. The secret-bearing conversions in this project
 *     are the `grdu_secret_*` pair, which is libsodium's and stays that way.
 *
 * Sizes: 512 bytes is about what a transaction body comes to on the Gradido Akademie ledger,
 * 64 is a short memo, and 4096 is the far end of what a memo may be.
 */

#include "arnm/converter.h"
#include "arnm/memory_block.h"
#include "arnm/mono_timer.h"
#include "bench_report.h"

#include <sodium.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMALL_SIZE 64u
#define BODY_SIZE 512u
#define LARGE_SIZE 4096u
#define LARGEST_SIZE LARGE_SIZE

/** @brief Random bytes, so neither encoder meets a run it could shortcut. */
static uint8_t payload[LARGEST_SIZE];
/** @brief The base64 of `payload`, at each of the three sizes, for the decoding steps. */
static char encoded_small[ARNM_BASE64_STRING_LENGTH(SMALL_SIZE) + 1u];
static char encoded_body[ARNM_BASE64_STRING_LENGTH(BODY_SIZE) + 1u];
static char encoded_large[ARNM_BASE64_STRING_LENGTH(LARGE_SIZE) + 1u];

/** @brief Where a step writes, large enough for the longest of either direction. */
static char text_buffer[ARNM_BASE64_STRING_LENGTH(LARGEST_SIZE) + 1u];
static uint8_t binary_buffer[LARGEST_SIZE];

/** @brief Kept so the optimiser cannot drop a conversion nothing reads. */
static uint64_t sink;

static void encode_arnm(uint32_t size, int steps) {
  const arnm_memory_block block = {payload, size};
  for (int i = 0; i < steps; ++i) {
    if (ARNM_SUCCESS != arnm_binary_to_base64(text_buffer, &block)) { abort(); }
    sink += (uint8_t)text_buffer[0];
  }
}

static void encode_sodium(uint32_t size, int steps) {
  const size_t room = sodium_base64_encoded_len(size, sodium_base64_VARIANT_ORIGINAL);
  for (int i = 0; i < steps; ++i) {
    if (!sodium_bin2base64(text_buffer, room, payload, size, sodium_base64_VARIANT_ORIGINAL)) {
      abort();
    }
    sink += (uint8_t)text_buffer[0];
  }
}

static void decode_arnm(const char *text, int steps) {
  for (int i = 0; i < steps; ++i) {
    uint32_t written = 0;
    if (ARNM_SUCCESS != arnm_binary_from_base64(binary_buffer, &written, text)) { abort(); }
    sink += written;
  }
}

static void decode_sodium(const char *text, int steps) {
  const size_t length = strlen(text);
  for (int i = 0; i < steps; ++i) {
    size_t written = 0;
    if (0 != sodium_base642bin(
                 binary_buffer, sizeof(binary_buffer), text, length, NULL, &written, NULL,
                 sodium_base64_VARIANT_ORIGINAL
             )) {
      abort();
    }
    sink += written;
  }
}

static void encode_arnm_small(int steps) {
  encode_arnm(SMALL_SIZE, steps);
}
static void encode_sodium_small(int steps) {
  encode_sodium(SMALL_SIZE, steps);
}
static void encode_arnm_body(int steps) {
  encode_arnm(BODY_SIZE, steps);
}
static void encode_sodium_body(int steps) {
  encode_sodium(BODY_SIZE, steps);
}
static void encode_arnm_large(int steps) {
  encode_arnm(LARGE_SIZE, steps);
}
static void encode_sodium_large(int steps) {
  encode_sodium(LARGE_SIZE, steps);
}

static void decode_arnm_small(int steps) {
  decode_arnm(encoded_small, steps);
}
static void decode_sodium_small(int steps) {
  decode_sodium(encoded_small, steps);
}
static void decode_arnm_body(int steps) {
  decode_arnm(encoded_body, steps);
}
static void decode_sodium_body(int steps) {
  decode_sodium(encoded_body, steps);
}
static void decode_arnm_large(int steps) {
  decode_arnm(encoded_large, steps);
}
static void decode_sodium_large(int steps) {
  decode_sodium(encoded_large, steps);
}

/**
 * @brief Fill the payload, encode it once at each size, and check the two agree.
 *
 * That the two agree at all is held by `Base64Test` in test_converter, which crosses both
 * directions at every length up to 200 and pins the RFC 4648 vectors against both. This is the
 * same question asked once more where the figures are taken, because a benchmark that measures
 * two implementations of different things measures nothing -- and a payload that only this file
 * builds is not covered by a test that builds its own.
 */
static void prepare_test_data(void) {
  srand(4711);
  for (uint32_t i = 0; i < LARGEST_SIZE; ++i) { payload[i] = (uint8_t)(rand() & 0xFF); }

  const struct {
    uint32_t size;
    char *out;
    size_t room;
  } cases[] = {
      {SMALL_SIZE, encoded_small, sizeof(encoded_small)},
      {BODY_SIZE, encoded_body, sizeof(encoded_body)},
      {LARGE_SIZE, encoded_large, sizeof(encoded_large)},
  };

  for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
    const arnm_memory_block block = {payload, cases[c].size};
    if (ARNM_SUCCESS != arnm_binary_to_base64(cases[c].out, &block)) { abort(); }

    char reference[ARNM_BASE64_STRING_LENGTH(LARGEST_SIZE) + 1u];
    if (!sodium_bin2base64(
            reference, sizeof(reference), payload, cases[c].size, sodium_base64_VARIANT_ORIGINAL
        )) {
      abort();
    }
    if (0 != strcmp(cases[c].out, reference)) {
      printf(
          "arnm and libsodium disagree at %u bytes -- the figures below mean nothing\n",
          cases[c].size
      );
      abort();
    }
  }
}

int main(void) {
  if (sodium_init() < 0) { return 1; }
  arnm_mono_timer_init();
  arnm_mono_timer timeUsed;
  arnm_mono_timer_reset(&timeUsed);
  prepare_test_data();
  bench_prepared(timeUsed);

  const int stepCount = 200000;

  printf(
      "\nsame text out of both, checked above; %u / %u / %u byte payloads\n", SMALL_SIZE, BODY_SIZE,
      LARGE_SIZE
  );

  bench_section("encode");
  bench_step(encode_arnm_small, stepCount, "  arnm       64 bytes", "conversion");
  bench_step(encode_sodium_small, stepCount, "  libsodium  64 bytes", "conversion");
  bench_step(encode_arnm_body, stepCount, "  arnm      512 bytes (a body)", "conversion");
  bench_step(encode_sodium_body, stepCount, "  libsodium 512 bytes (a body)", "conversion");
  bench_step(encode_arnm_large, stepCount, "  arnm     4096 bytes", "conversion");
  bench_step(encode_sodium_large, stepCount, "  libsodium 4096 bytes", "conversion");

  bench_section("decode");
  bench_step(decode_arnm_small, stepCount, "  arnm       64 bytes", "conversion");
  bench_step(decode_sodium_small, stepCount, "  libsodium  64 bytes", "conversion");
  bench_step(decode_arnm_body, stepCount, "  arnm      512 bytes (a body)", "conversion");
  bench_step(decode_sodium_body, stepCount, "  libsodium 512 bytes (a body)", "conversion");
  bench_step(decode_arnm_large, stepCount, "  arnm     4096 bytes", "conversion");
  bench_step(decode_sodium_large, stepCount, "  libsodium 4096 bytes", "conversion");

  bench_total(timeUsed, stepCount, "conversion");

  // read once, so nothing above is a conversion the optimiser was free to skip
  if (0 == sink) { printf("(sink was zero)\n"); }
  return 0;
}
