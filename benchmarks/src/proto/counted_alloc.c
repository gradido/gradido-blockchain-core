#include "proto_common.h"

#include <stdlib.h>

/*
 * Layout of a counted block:
 *
 *   plain:   [size u64][pad u64][user bytes ...]            user = raw + 16
 *   aligned: [...pad...][size u64][raw ptr][user bytes ...]  user aligned, user - 8 = raw
 *
 * Both keep the size at user - 16, so the accounting reads it the same way. The aligned form
 * also keeps the pointer malloc handed out at user - 8, because the padding in front of the
 * header depends on an alignment its free call is not told.
 */

#define COUNTED_HEADER_SIZE 16u

static proto_counted_stats g_stats;

static void counted_add(uint64_t size) {
  g_stats.current_bytes += size;
  g_stats.allocations++;
  if (g_stats.current_bytes > g_stats.peak_bytes) { g_stats.peak_bytes = g_stats.current_bytes; }
}

static uint64_t counted_size_of(void *user) {
  uint64_t size;
  memcpy(&size, (uint8_t *)user - COUNTED_HEADER_SIZE, sizeof(size));
  return size;
}

void *proto_counted_malloc(size_t size) {
  uint8_t *raw = (uint8_t *)malloc(size + COUNTED_HEADER_SIZE);
  if (!raw) { return NULL; }
  uint64_t stored = size;
  memcpy(raw, &stored, sizeof(stored));
  counted_add(stored);
  g_stats.current_blocks++;
  return raw + COUNTED_HEADER_SIZE;
}

void *proto_counted_calloc(size_t count, size_t size) {
  if (size && count > SIZE_MAX / size) { return NULL; }
  void *user = proto_counted_malloc(count * size);
  if (user) { memset(user, 0, count * size); }
  return user;
}

void *proto_counted_realloc(void *pointer, size_t size) {
  if (!pointer) { return proto_counted_malloc(size); }
  if (!size) {
    proto_counted_free(pointer);
    return NULL;
  }
  uint64_t old_size = counted_size_of(pointer);
  uint8_t *raw =
      (uint8_t *)realloc((uint8_t *)pointer - COUNTED_HEADER_SIZE, size + COUNTED_HEADER_SIZE);
  if (!raw) { return NULL; }
  uint64_t stored = size;
  memcpy(raw, &stored, sizeof(stored));
  g_stats.current_bytes -= old_size;
  counted_add(stored);
  return raw + COUNTED_HEADER_SIZE;
}

void proto_counted_free(void *pointer) {
  if (!pointer) { return; }
  g_stats.current_bytes -= counted_size_of(pointer);
  g_stats.current_blocks--;
  free((uint8_t *)pointer - COUNTED_HEADER_SIZE);
}

void *proto_counted_aligned_malloc(size_t alignment, size_t size) {
  if (alignment < sizeof(void *)) { alignment = sizeof(void *); }
  // header rounded up to the alignment, so the user pointer lands on a multiple of it
  size_t front = (COUNTED_HEADER_SIZE + alignment - 1) & ~(alignment - 1);
  uint8_t *raw = (uint8_t *)malloc(size + front + alignment);
  if (!raw) { return NULL; }
  uintptr_t user_address = ((uintptr_t)raw + front + alignment - 1) & ~(uintptr_t)(alignment - 1);
  uint8_t *user = (uint8_t *)user_address;
  uint64_t stored = size;
  memcpy(user - COUNTED_HEADER_SIZE, &stored, sizeof(stored));
  memcpy(user - 8, &raw, sizeof(raw));
  counted_add(stored);
  g_stats.current_blocks++;
  return user;
}

void proto_counted_aligned_free(void *pointer) {
  if (!pointer) { return; }
  g_stats.current_bytes -= counted_size_of(pointer);
  g_stats.current_blocks--;
  uint8_t *raw;
  memcpy(&raw, (uint8_t *)pointer - 8, sizeof(raw));
  free(raw);
}

proto_counted_stats proto_counted_get_stats(void) {
  return g_stats;
}

void proto_counted_reset_peak(void) {
  g_stats.peak_bytes = g_stats.current_bytes;
  g_stats.allocations = 0;
}
