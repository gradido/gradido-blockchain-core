#include "gradido_blockchain_core/utils/converter.h"
#include "hostmem/memory.h"
#include "hostmem/result.h"
#include <stdint.h>

#ifdef USE_SODIUM
#include "sodium.h"
#endif // USE_SODIUM

#include <assert.h>
#include <string.h>

/*
 * C11 static assert fallback safety
 */
#if !defined(static_assert)
#define static_assert _Static_assert
#endif

static_assert(UUID_BINARY_SIZE == 16, "uuid binary size don't match 16 bytes");

/* Value of a hex digit, 0xFF for every byte that is not one. Used by the uuid parser, whose
   16 bytes sit at fixed scattered positions -- a shape a vectoriser cannot help with, so the
   table wins there while the bulk conversions below do better computing the digits. The sentinel is
   the point: it turns "is this a hex digit" into a single test whose result can be OR-ed into a
   running verdict, instead of the two comparisons per nibble a zero-for-invalid table forces.
   Spelled out rather than filled with a range designator, which is a GNU extension MSVC does not
   have
   -- and MSVC is the reason the CMake build exists. */
static const uint8_t UUID_HEX_VALUE[256] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

/* Both characters of every byte value, so one two byte copy per byte replaces a pair of nibble
   lookups and lands them at their final position in one go. */
static const char UUID_HEX_PAIR[256][2] = {
    {'0', '0'}, {'0', '1'}, {'0', '2'}, {'0', '3'}, {'0', '4'}, {'0', '5'}, {'0', '6'}, {'0', '7'},
    {'0', '8'}, {'0', '9'}, {'0', 'a'}, {'0', 'b'}, {'0', 'c'}, {'0', 'd'}, {'0', 'e'}, {'0', 'f'},
    {'1', '0'}, {'1', '1'}, {'1', '2'}, {'1', '3'}, {'1', '4'}, {'1', '5'}, {'1', '6'}, {'1', '7'},
    {'1', '8'}, {'1', '9'}, {'1', 'a'}, {'1', 'b'}, {'1', 'c'}, {'1', 'd'}, {'1', 'e'}, {'1', 'f'},
    {'2', '0'}, {'2', '1'}, {'2', '2'}, {'2', '3'}, {'2', '4'}, {'2', '5'}, {'2', '6'}, {'2', '7'},
    {'2', '8'}, {'2', '9'}, {'2', 'a'}, {'2', 'b'}, {'2', 'c'}, {'2', 'd'}, {'2', 'e'}, {'2', 'f'},
    {'3', '0'}, {'3', '1'}, {'3', '2'}, {'3', '3'}, {'3', '4'}, {'3', '5'}, {'3', '6'}, {'3', '7'},
    {'3', '8'}, {'3', '9'}, {'3', 'a'}, {'3', 'b'}, {'3', 'c'}, {'3', 'd'}, {'3', 'e'}, {'3', 'f'},
    {'4', '0'}, {'4', '1'}, {'4', '2'}, {'4', '3'}, {'4', '4'}, {'4', '5'}, {'4', '6'}, {'4', '7'},
    {'4', '8'}, {'4', '9'}, {'4', 'a'}, {'4', 'b'}, {'4', 'c'}, {'4', 'd'}, {'4', 'e'}, {'4', 'f'},
    {'5', '0'}, {'5', '1'}, {'5', '2'}, {'5', '3'}, {'5', '4'}, {'5', '5'}, {'5', '6'}, {'5', '7'},
    {'5', '8'}, {'5', '9'}, {'5', 'a'}, {'5', 'b'}, {'5', 'c'}, {'5', 'd'}, {'5', 'e'}, {'5', 'f'},
    {'6', '0'}, {'6', '1'}, {'6', '2'}, {'6', '3'}, {'6', '4'}, {'6', '5'}, {'6', '6'}, {'6', '7'},
    {'6', '8'}, {'6', '9'}, {'6', 'a'}, {'6', 'b'}, {'6', 'c'}, {'6', 'd'}, {'6', 'e'}, {'6', 'f'},
    {'7', '0'}, {'7', '1'}, {'7', '2'}, {'7', '3'}, {'7', '4'}, {'7', '5'}, {'7', '6'}, {'7', '7'},
    {'7', '8'}, {'7', '9'}, {'7', 'a'}, {'7', 'b'}, {'7', 'c'}, {'7', 'd'}, {'7', 'e'}, {'7', 'f'},
    {'8', '0'}, {'8', '1'}, {'8', '2'}, {'8', '3'}, {'8', '4'}, {'8', '5'}, {'8', '6'}, {'8', '7'},
    {'8', '8'}, {'8', '9'}, {'8', 'a'}, {'8', 'b'}, {'8', 'c'}, {'8', 'd'}, {'8', 'e'}, {'8', 'f'},
    {'9', '0'}, {'9', '1'}, {'9', '2'}, {'9', '3'}, {'9', '4'}, {'9', '5'}, {'9', '6'}, {'9', '7'},
    {'9', '8'}, {'9', '9'}, {'9', 'a'}, {'9', 'b'}, {'9', 'c'}, {'9', 'd'}, {'9', 'e'}, {'9', 'f'},
    {'a', '0'}, {'a', '1'}, {'a', '2'}, {'a', '3'}, {'a', '4'}, {'a', '5'}, {'a', '6'}, {'a', '7'},
    {'a', '8'}, {'a', '9'}, {'a', 'a'}, {'a', 'b'}, {'a', 'c'}, {'a', 'd'}, {'a', 'e'}, {'a', 'f'},
    {'b', '0'}, {'b', '1'}, {'b', '2'}, {'b', '3'}, {'b', '4'}, {'b', '5'}, {'b', '6'}, {'b', '7'},
    {'b', '8'}, {'b', '9'}, {'b', 'a'}, {'b', 'b'}, {'b', 'c'}, {'b', 'd'}, {'b', 'e'}, {'b', 'f'},
    {'c', '0'}, {'c', '1'}, {'c', '2'}, {'c', '3'}, {'c', '4'}, {'c', '5'}, {'c', '6'}, {'c', '7'},
    {'c', '8'}, {'c', '9'}, {'c', 'a'}, {'c', 'b'}, {'c', 'c'}, {'c', 'd'}, {'c', 'e'}, {'c', 'f'},
    {'d', '0'}, {'d', '1'}, {'d', '2'}, {'d', '3'}, {'d', '4'}, {'d', '5'}, {'d', '6'}, {'d', '7'},
    {'d', '8'}, {'d', '9'}, {'d', 'a'}, {'d', 'b'}, {'d', 'c'}, {'d', 'd'}, {'d', 'e'}, {'d', 'f'},
    {'e', '0'}, {'e', '1'}, {'e', '2'}, {'e', '3'}, {'e', '4'}, {'e', '5'}, {'e', '6'}, {'e', '7'},
    {'e', '8'}, {'e', '9'}, {'e', 'a'}, {'e', 'b'}, {'e', 'c'}, {'e', 'd'}, {'e', 'e'}, {'e', 'f'},
    {'f', '0'}, {'f', '1'}, {'f', '2'}, {'f', '3'}, {'f', '4'}, {'f', '5'}, {'f', '6'}, {'f', '7'},
    {'f', '8'}, {'f', '9'}, {'f', 'a'}, {'f', 'b'}, {'f', 'c'}, {'f', 'd'}, {'f', 'e'}, {'f', 'f'},
};

/* Where each byte's first hex character sits in the 8-4-4-4-12 layout; the second follows
   directly after it. The four separators sit at 8, 13, 18 and 23. Driving the loops from this
   table is what removes the per-character branching the previous version needed: the format is
   fixed, so the positions never have to be discovered while reading. */
static const uint8_t UUID_HEX_POS[UUID_BINARY_SIZE] = {0,  2,  4,  6,  9,  11, 14, 16,
                                                       19, 21, 24, 26, 28, 30, 32, 34};

hostmem_result grdu_uuid_from_string(uint8_t *uuid, const char *uuid_string) {
  if (!uuid || !uuid_string) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (strlen(uuid_string) != 36) { return HOSTMEM_ERROR_INVALID_PARAM; }

  // The separators are checked by position, not merely counted. Skipping any dash wherever it
  // appeared -- what this function used to do -- let a 36 character string carry fewer than
  // four of them, and every missing dash turned two characters into an extra output byte: an
  // all hex string of the right length wrote 18 bytes into these 16.
  if (uuid_string[8] != '-' || uuid_string[13] != '-' || uuid_string[18] != '-' ||
      uuid_string[23] != '-') {
    memset(uuid, 0, UUID_BINARY_SIZE);
    return HOSTMEM_ERROR_DECODE_FAILED;
  }

  // Decoding writes straight into the caller's buffer and the verdict is settled once at the
  // end: a bad digit shows up as 0xFF, whose high nibble survives the OR no matter what else
  // the string held. Nothing branches on the data in between.
  unsigned invalid = 0;
  for (size_t k = 0; k < UUID_BINARY_SIZE; ++k) {
    unsigned high = UUID_HEX_VALUE[(unsigned char)uuid_string[UUID_HEX_POS[k]]];
    unsigned low = UUID_HEX_VALUE[(unsigned char)uuid_string[UUID_HEX_POS[k] + 1]];
    invalid |= high | low;
    uuid[k] = (uint8_t)((high << 4) | low);
  }

  // Half decoded bytes are worth less than nothing to a caller who ignores the result code, so
  // the failure path clears them. It costs nothing where it matters: this runs only when the
  // string was already rejected.
  if (invalid & 0xF0u) {
    memset(uuid, 0, UUID_BINARY_SIZE);
    return HOSTMEM_ERROR_DECODE_FAILED;
  }
  return HOSTMEM_SUCCESS;
}

void grdu_uuid_to_string(char *result_buffer, const uint8_t uuid[UUID_BINARY_SIZE]) {
  // Writes each byte where it belongs immediately. The previous version formatted all 32
  // characters into a scratch buffer with sodium_bin2hex and then reassembled them around the
  // separators with five memcpy calls, which walked the result twice; this also leaves the
  // function free of libsodium, so it no longer needs USE_SODIUM to exist.
  for (size_t k = 0; k < UUID_BINARY_SIZE; ++k) {
    memcpy(result_buffer + UUID_HEX_POS[k], UUID_HEX_PAIR[uuid[k]], 2);
  }
  result_buffer[8] = '-';
  result_buffer[13] = '-';
  result_buffer[18] = '-';
  result_buffer[23] = '-';
  result_buffer[36] = '\0';
}

hostmem_result grdu_binary_to_hex(char *result_buffer, const hostmem_memory_block *data) {
  if (!result_buffer || !data || !data->data) { return HOSTMEM_ERROR_NULL_POINTER; }
  // an empty block is a parameter the caller can fix, not a pointer they forgot
  if (!data->size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  // Computed, not looked up. A table would be a gather no vectoriser can follow, while this is
  // a comparison and an add per nibble -- the compiler turns the conditional into a select and
  // runs the loop a vector register at a time. Staying in uint8_t is what makes that happen:
  // the same expression written over int costs a sign extension per element and loses it.
  // Read out of the block before the loop: result_buffer is a char pointer, which is allowed to
  // alias anything, so a store through it forces the compiler to assume data->size and
  // data->data may have changed. Reloading them every iteration is what stops it vectorising.
  const uint8_t *bytes = data->data;
  const size_t count = data->size;

  for (size_t i = 0; i < count; ++i) {
    uint8_t high = (uint8_t)(bytes[i] >> 4);
    uint8_t low = (uint8_t)(bytes[i] & 0x0F);
    result_buffer[i * 2] = (char)(uint8_t)(high + (high < 10 ? 48 : 87));
    result_buffer[i * 2 + 1] = (char)(uint8_t)(low + (low < 10 ? 48 : 87));
  }
  result_buffer[count * 2] = '\0';
  return HOSTMEM_SUCCESS;
}

hostmem_result grdu_binary_from_hex(uint8_t *result_buffer, const char *hex) {
  if (!result_buffer || !hex) { return HOSTMEM_ERROR_NULL_POINTER; }
  size_t hex_size = strlen(hex);
  size_t bin_size = hex_size / 2;
  // two characters make one byte, so an odd length cannot be hex -- the division above dropped
  // the stray character and multiplying back reveals it
  if (bin_size * 2 != hex_size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  // Same reasoning as the encoding direction, with the validity test folded in. Clearing bit 5
  // maps a lower case letter onto its upper case twin, so one range check covers both; a digit
  // is its own range. The nibble itself falls out of (c & 0xF) + 9 * (c >> 6), since bit 6 is
  // set for letters and clear for digits. Invalid characters produce a value here as well --
  // that is what makes the loop branchless -- and the verdict below throws it away.
  uint8_t invalid = 0;
  for (size_t i = 0; i < bin_size; ++i) {
    uint8_t high_char = (uint8_t)hex[i * 2];
    uint8_t low_char = (uint8_t)hex[i * 2 + 1];
    uint8_t high_letter = (uint8_t)(high_char & 0xDF);
    uint8_t low_letter = (uint8_t)(low_char & 0xDF);

    invalid |= (uint8_t)(1u - (unsigned)(((high_char >= '0') & (high_char <= '9')) |
                                         ((high_letter >= 'A') & (high_letter <= 'F'))));
    invalid |= (uint8_t)(1u - (unsigned)(((low_char >= '0') & (low_char <= '9')) |
                                         ((low_letter >= 'A') & (low_letter <= 'F'))));

    uint8_t high = (uint8_t)((high_char & 0x0F) + 9u * (unsigned)(high_char >> 6));
    uint8_t low = (uint8_t)((low_char & 0x0F) + 9u * (unsigned)(low_char >> 6));
    result_buffer[i] = (uint8_t)((high << 4) | low);
  }

  if (invalid) {
    memset(result_buffer, 0, bin_size);
    return HOSTMEM_ERROR_DECODE_FAILED;
  }
  return HOSTMEM_SUCCESS;
}

#ifdef USE_SODIUM

/*
 * The same two conversions for material that has to keep quiet about itself, handed to
 * libsodium instead of done here.
 *
 * What they buy: libsodium computes each digit with arithmetic chosen so that no branch and no
 * memory access follows the value being converted, and it is tested and reviewed for holding
 * that. The fast pair above computes its digits too, but only the vectorised half of the
 * generated code stays branchless -- the scalar path beside it, which takes the remainder and
 * takes short inputs whole, compiles to a compare and a jump on the nibble, and an unoptimised
 * build has no vector path at all. Writing the conditional as an arithmetic mask does not fix
 * it; the compiler turns that back into a branch as well.
 *
 * What they cost, measured over 32 bytes in a ReleaseFast build: 12.2 ns against 5.6 for
 * encoding, 91.2 ns against 9.1 for decoding. Roughly twice the time one way and ten times the
 * other -- the decoding gap is the wider one because sodium_hex2bin carries a state machine
 * that cannot vectorise at all.
 *
 * Use these when the bytes are a key, a seed or a passphrase. Use the pair above for hashes,
 * transaction ids, public keys and anything else already public. Both pairs answer with the
 * same result codes, so swapping one for the other changes only the timing.
 *
 * Neither pair is the whole story for a secret: wiping the caller's buffers afterwards and
 * keeping them out of swap is still the caller's part.
 */
hostmem_result grdu_secret_to_hex(char *result_buffer, const hostmem_memory_block *data) {
  if (!result_buffer || !data || !data->data) { return HOSTMEM_ERROR_NULL_POINTER; }
  if (!data->size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  sodium_bin2hex(result_buffer, (size_t)data->size * 2 + 1, data->data, data->size);
  return HOSTMEM_SUCCESS;
}

hostmem_result grdu_secret_from_hex(uint8_t *result_buffer, const char *hex) {
  if (!result_buffer || !hex) { return HOSTMEM_ERROR_NULL_POINTER; }
  size_t hex_size = strlen(hex);
  size_t bin_size = hex_size / 2;
  if (bin_size * 2 != hex_size) { return HOSTMEM_ERROR_INVALID_PARAM; }

  size_t written = 0;
  if (0 != sodium_hex2bin(result_buffer, bin_size, hex, hex_size, NULL, &written, NULL)) {
    // sodium_hex2bin reports a length of zero on failure but leaves whatever it managed to
    // decode standing in the buffer. sodium_memzero rather than memset: clearing a buffer
    // nobody reads again is exactly what a compiler is allowed to drop, and that is the one
    // place it must not.
    sodium_memzero(result_buffer, bin_size);
    return HOSTMEM_ERROR_DECODE_FAILED;
  }
  return HOSTMEM_SUCCESS;
}

const static int BASE64_VARIANT = sodium_base64_VARIANT_ORIGINAL;

size_t grdu_binary_to_base64_length(size_t binSize) {
  return sodium_base64_encoded_len(binSize, BASE64_VARIANT);
}

hostmem_result grdu_binary_to_base64_with_known_size(
    hostmem_memory_block *result_block, const hostmem_memory_block *data
) {
  if (!result_block || !result_block->data || !data || !data->data) {
    return HOSTMEM_ERROR_NULL_POINTER;
  }
  // sodium_bin2base64 does not report a destination that is too small: it calls
  // sodium_misuse(), which aborts the process. Checking the room here is the only way to turn
  // a caller's miscalculation into a result they can handle — and the reason the NULL check
  // that used to stand here was unreachable.
  if (result_block->size < sodium_base64_encoded_len(data->size, BASE64_VARIANT)) {
    return HOSTMEM_ERROR_DESTINATION_BUFFER_TO_SMALL;
  }
  sodium_bin2base64(
      (char *)result_block->data, result_block->size, data->data, data->size, BASE64_VARIANT
  );
  return HOSTMEM_SUCCESS;
}

hostmem_result grdu_binary_to_base64(
    hostmem_memory_block *result_block, const hostmem_memory_block *data, hostmem *allocator
) {
  if (!result_block || !data || !allocator) { return HOSTMEM_ERROR_NULL_POINTER; }
  size_t strSize = sodium_base64_encoded_len(data->size, BASE64_VARIANT);
  if (strSize > UINT32_MAX - 7) { return HOSTMEM_ERROR_ARITHMETIC_OVERFLOW; }
  hostmem_result result = hostmem_memory_block_alloc(result_block, (uint32_t)strSize, allocator);
  if (result != HOSTMEM_SUCCESS) { return result; }

  return grdu_binary_to_base64_with_known_size(result_block, data);
}

size_t grdu_binary_from_base64(hostmem_memory_block *result_block, const char *base64_str) {
  if (!result_block || !base64_str) { return 0; }
  size_t result_bin_size = 0;
  if (sodium_base642bin(
          result_block->data, result_block->size, base64_str, strlen(base64_str), NULL,
          &result_bin_size, NULL, BASE64_VARIANT
      )) {
    return 0;
  }
  return result_bin_size;
}

#endif // USE_SODIUM
