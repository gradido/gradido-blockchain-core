#include "gradido_blockchain_core/result.h"

#include "hostmem/result.h"

const char *grd_result_to_string(hostmem_result result) {
  // the project's own range first; everything else is hostmem's to name
  switch ((int)result) {
  case GRD_ERROR_PB_UNHANDLED_ONEOF_BRANCH:
    return "GRD_ERROR_PB_UNHANDLED_ONEOF_BRANCH";
  case GRD_ERROR_PB_UNHANDLED_PARAMETER:
    return "GRD_ERROR_PB_UNHANDLED_PARAMETER";
  case GRD_ERROR_PB_INCORRECT_VERSION:
    return "GRD_ERROR_PB_INCORRECT_VERSION";
  default:
    return hostmem_result_to_string(result);
  }
}
