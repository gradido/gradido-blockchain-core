#ifndef GRADIDO_BLOCKCHAIN_CORE_RESULT_H
#define GRADIDO_BLOCKCHAIN_CORE_RESULT_H

#include "hostmem/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grd_pb_result grd_pb_result
 *  @brief The project's own outcome codes, counted on top of hostmem's.
 *
 *  Every function here returns @ref hostmem_result. hostmem owns the general codes — success,
 *  the arena warning, null pointer, overflow — and reserves everything from
 *  @ref HOSTMEM_ERROR_USER_BASE upwards for the project embedding it. The protobuf codes below
 *  live in that reserved range, so the two sets can grow without ever meeting.
 *
 *  @{
 */

/** @brief Outcomes that only the protobuf mapping can produce. */
enum grd_pb_result {
  GRD_ERROR_PB_UNHANDLED_ONEOF_BRANCH = HOSTMEM_ERROR_USER_BASE,
  GRD_ERROR_PB_UNHANDLED_PARAMETER,
  GRD_ERROR_PB_INCORRECT_VERSION
};

/** @brief Name of a result code, from either range.
 *
 *  @param[in] result Any @ref hostmem_result, including the codes above.
 *  @return Static string with the enumerator's name, never NULL.
 *  @note Codes hostmem owns are passed through to hostmem_result_to_string().
 */
const char *grd_result_to_string(hostmem_result result);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_RESULT_H
