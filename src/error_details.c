#include "gradido_blockchain_core/error_details.h"
#include "arnm/converter.h"
#include "arnm/memory.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/utils/converter.h"

#include <stdlib.h>
#include <string.h>

arnm_result grd_error_details_init(grd_error_details *error_details, arnm *alloc) {
  if (!error_details) { return ARNM_ERROR_NULL_POINTER; }
  memset(error_details, 0, sizeof(grd_error_details));
  error_details->allocator = alloc;
  return ARNM_SUCCESS;
}

grd_error_details *grd_error_details_create(arnm *alloc) {
  grd_error_details *error_details = (grd_error_details *)malloc(sizeof(grd_error_details));
  grd_error_details_init(error_details, alloc);
  return error_details;
}

int grd_error_details_is_initalized_and_empty(grd_error_details *error_details) {
  return error_details && !error_details->message && !error_details->actual &&
         !error_details->expected && !error_details->used_default_malloc_flag;
}

static int alloc_and_fill_field(char **field, const char *input, arnm *alloc, int flag) {
  size_t size = strlen(input) + 1;
  int result_alloc_flag = 0;
  if (arnm_alloc((uint8_t **)field, (uint32_t)size, alloc) != ARNM_SUCCESS) {
    *field = (char *)malloc(size);
    result_alloc_flag = flag;
  }
  memcpy(*field, input, size);
  return result_alloc_flag;
}

arnm_result grd_error_details_fill(
    grd_error_details *error_details, const char *message, const char *actual, const char *expected
) {
  if (!error_details) { return ARNM_ERROR_NULL_POINTER; }
  if (error_details->message || error_details->actual || error_details->expected) {
    return ARNM_ERROR_INVALID_STATE;
  }
  if (message) {
    error_details->used_default_malloc_flag |=
        alloc_and_fill_field(&error_details->message, message, error_details->allocator, 1);
  }
  if (actual) {
    error_details->used_default_malloc_flag |=
        alloc_and_fill_field(&error_details->actual, actual, error_details->allocator, 2);
  }
  if (expected) {
    error_details->used_default_malloc_flag |=
        alloc_and_fill_field(&error_details->expected, expected, error_details->allocator, 4);
  }
  return ARNM_SUCCESS;
}

arnm_result grd_error_details_fill_actual_is_number(
    grd_error_details *error_details,
    const char *message,
    const int64_t actual,
    const char *expected
) {
  if (!error_details) { return ARNM_ERROR_NULL_POINTER; }
  if (error_details->message || error_details->actual || error_details->expected) {
    return ARNM_ERROR_INVALID_STATE;
  }

  char strBuffer[22];
  memset(strBuffer, 0, 22);
  int strLen = arnm_int64_to_string(strBuffer, 22, actual);
  return grd_error_details_fill(error_details, message, strBuffer, expected);
}

static void release_field(char *field, grd_error_details *error_details, int field_flag) {
  if (!field || !error_details || !field_flag) { return; }
  if (field_flag == (field_flag & error_details->used_default_malloc_flag)) {
    free(field);
  } else {
    // the field is a NUL terminated copy, so its allocated size is recoverable
    arnm_free((uint8_t *)field, (uint32_t)strlen(field) + 1, error_details->allocator);
  }
}

const char *grd_error_details_get_message(const grd_error_details *error_details) {
  if (!error_details) { return NULL; }
  return error_details->message;
}

const char *grd_error_details_get_actual(const grd_error_details *error_details) {
  if (!error_details) { return NULL; }
  return error_details->actual;
}

const char *grd_error_details_get_expected(const grd_error_details *error_details) {
  if (!error_details) { return NULL; }
  return error_details->expected;
}

void grd_error_details_release(grd_error_details *error_details) {
  if (!error_details) { return; }

  release_field(error_details->expected, error_details, 4);
  release_field(error_details->actual, error_details, 2);
  release_field(error_details->message, error_details, 1);
}

void grd_error_details_free(grd_error_details *error_details) {
  if (!error_details) { return; }
  grd_error_details_release(error_details);
  free(error_details);
}
