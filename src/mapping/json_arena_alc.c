#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_ARENA_ALC_C
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_ARENA_ALC_C

#include "hostmem/multi_arena.h"
#include "hostmem/result.h"

#include "yyjson.h"

#include <stdint.h>
#include <string.h>

/**
 * @file
 * @brief Included, not compiled: a @ref hostmem_multi_arena behind yyjson's allocator interface.
 *
 * Both directions need this -- the writer to build a document and write it out, the reader to
 * parse one -- and neither wants the other's code, so it sits here on its own rather than in
 * either. See json_writer.c for why these fragments are `.c` files and not headers: they need
 * yyjson, and a header in `include/gradido_blockchain_core` would put yyjson on the include path
 * of everything that touches this library.
 *
 * yyjson asks for memory through three function pointers and a context. The context here is a
 * chain, so every byte a render or a read needs -- the document, the value pool, the writer's
 * output buffer -- is drawn from the chain the caller opened, and malloc is never reached. A
 * bump chain hands memory back in one motion rather than block by block, which is what shapes
 * the three below.
 *
 * @whisper One stream, tapped by whoever is working
 */

static void *grdm_json_arena_malloc(void *ctx, size_t size) {
  hostmem_multi_arena *chain = (hostmem_multi_arena *)ctx;
  if (size > UINT32_MAX) { return NULL; }
  // NULL is how this interface says "no memory", so a zero sized request cannot answer with it
  // and asks for one byte instead -- which the arena rounds up to its alignment anyway
  uint8_t *buffer = NULL;
  if (HOSTMEM_SUCCESS != hostmem_multi_arena_alloc(&buffer, size ? (uint32_t)size : 1, chain)) {
    return NULL;
  }
  return buffer;
}

static void *grdm_json_arena_realloc(void *ctx, void *ptr, size_t old_size, size_t size) {
  if (!ptr) { return grdm_json_arena_malloc(ctx, size); }
  // the block already reaches that far; a bump allocator cannot give the difference back, and
  // the reservation stays what it was
  if (size <= old_size) { return ptr; }
  void *grown = grdm_json_arena_malloc(ctx, size);
  if (!grown) { return NULL; }
  memcpy(grown, ptr, old_size);
  return grown;
}

static void grdm_json_arena_free(void *ctx, void *ptr) {
  // Deliberately empty. hostmem_multi_arena_free() reclaims a block only while it is the tail
  // of its own arena, and it needs the size to do it -- a size this interface does not carry.
  // So nothing is attempted: the chain comes back whole at the caller's
  // hostmem_multi_arena_reset(), which is what the public headers promise.
  (void)ctx;
  (void)ptr;
}

/** @brief yyjson's allocator, drawing from @p chain. Held by value wherever yyjson keeps it. */
static yyjson_alc grdm_json_arena_alc(hostmem_multi_arena *chain) {
  const yyjson_alc alc = {
      grdm_json_arena_malloc, grdm_json_arena_realloc, grdm_json_arena_free, chain
  };
  return alc;
}

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_ARENA_ALC_C
