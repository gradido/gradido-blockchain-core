#ifndef GRADIDO_BLOCKCHAIN_CORE_TYPES_CROSS_GROUP_H
#define GRADIDO_BLOCKCHAIN_CORE_TYPES_CROSS_GROUP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum grdt_cross_group {
  GRDT_CROSS_GROUP_LOCAL = 0,
  GRDT_CROSS_GROUP_INBOUND = 1,
  GRDT_CROSS_GROUP_OUTBOUND = 2,
  GRDT_CROSS_GROUP_CROSS = 3,
  /* as error value, on this place, because 0 - 3 already pinned to protobuf enum values */
  GRDT_CROSS_GROUP_NONE = 4
} grdt_cross_group;

const char *grdt_cross_group_to_string(grdt_cross_group cross_group);
grdt_cross_group grdt_cross_group_from_string(const char *cross_group_string, size_t string_size);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_TYPES_CROSS_GROUP_H
