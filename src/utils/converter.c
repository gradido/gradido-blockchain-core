#include "gradido_blockchain_core/utils/converter.h"
#include "hostmem/memory.h"
#include "hostmem/result.h"
#include <stdint.h>

#ifdef USE_SODIUM
#include "sodium.h"
#endif // USE_SODIUM

#include <string.h>

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
