#ifndef GRADIDO_BLOCKCHAIN_CORE_RESULT_H
#define GRADIDO_BLOCKCHAIN_CORE_RESULT_H


#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grd_result grd_result
 *
 *  @{
 */

typedef enum grd_result {
    GRD_SUCCESS = 0,

    GRD_ERROR_NOT_INITIALIZED = 1,
    GRD_ERROR_INVALID_PARAM = 2, // if parameter validation failed
    GRD_ERROR_NULL_POINTER = 3,
    GRD_ERROR_ARITHMETIC_OVERFLOW = 4,
    GRD_ERROR_OUT_OF_MEMORY = 5,

} grd_result;

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_RESULT_H
