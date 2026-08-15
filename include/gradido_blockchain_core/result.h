#ifndef GRADIDO_BLOCKCHAIN_CORE_RESULT_H
#define GRADIDO_BLOCKCHAIN_CORE_RESULT_H

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grd_result grd_result
 *
 *  @{
 */

/** @brief Outcome of an operation: success, a warning, or an error.
 *
 *  The order of the enumerators is part of the contract. Success is 0, warnings
 *  follow, and every error comes after @ref GRD_ERROR_NOT_IMPLEMENTED_YET.
 *  Keep new warnings in the warning block and new errors
 *  after it. Please handle warnings explicit in the code.
 */
typedef enum grd_result {
  GRD_SUCCESS = 0,

  // warnings: the operation was carried out, with a caveat worth reporting
  GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED,
  GRD_WARNING_USED_DYNAMIC_ALLOCATION_FALLBACK,

  // errors: the operation was not carried out
  GRD_ERROR_NOT_IMPLEMENTED_YET,
  GRD_ERROR_NOT_INITIALIZED,
  GRD_ERROR_INVALID_PARAM,     // if parameter validation failed
  GRD_ERROR_INVALID_ENUM_TYPE, // enum type invalid for function call
  GRD_ERROR_INVALID_STATE,
  GRD_ERROR_NULL_POINTER,
  GRD_ERROR_ARITHMETIC_OVERFLOW,
  GRD_ERROR_OUT_OF_MEMORY,
  GRD_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS,

  GRD_ERROR_DECODE_FAILED,
  GRD_ERROR_ENCODE_FAILED,
  GRD_ERROR_DESTINATION_BUFFER_TO_SMALL,

  // enum
  GRD_ERROR_ENUM_UNHANDLED,
  GRD_ERROR_ENUM_UNKNOWN,

  // protobuf
  GRD_ERROR_PB_UNHANDLED_ONEOF_BRANCH,
  GRD_ERROR_PB_UNHANDLED_PARAMETER,
  GRD_ERROR_PB_INCORRECT_VERSION
} grd_result;

const char *grd_result_to_string(grd_result result);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_RESULT_H
