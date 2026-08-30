#include "gradido_blockchain_core/types/cross_group.h"
#include "gradido_blockchain_core/utils/string_helper.h"

#include <stdint.h>
#include <string.h>

#define GRDT_CROSS_GROUP_LOCAL_STRING "GRDT_CROSS_GROUP_LOCAL"
#define GRDT_CROSS_GROUP_INBOUND_STRING "GRDT_CROSS_GROUP_INBOUND"
#define GRDT_CROSS_GROUP_OUTBOUND_STRING "GRDT_CROSS_GROUP_OUTBOUND"
#define GRDT_CROSS_GROUP_CROSS_STRING "GRDT_CROSS_GROUP_CROSS"
#define GRDT_CROSS_GROUP_NONE_STRING "GRDT_CROSS_GROUP_NONE"

const char *grdt_cross_group_to_string(grdt_cross_group cross_group) {
  static const char *messages[] = {
      [GRDT_CROSS_GROUP_LOCAL] = GRDT_CROSS_GROUP_LOCAL_STRING,
      [GRDT_CROSS_GROUP_INBOUND] = GRDT_CROSS_GROUP_INBOUND_STRING,
      [GRDT_CROSS_GROUP_OUTBOUND] = GRDT_CROSS_GROUP_OUTBOUND_STRING,
      [GRDT_CROSS_GROUP_CROSS] = GRDT_CROSS_GROUP_CROSS_STRING,
      [GRDT_CROSS_GROUP_NONE] = GRDT_CROSS_GROUP_NONE_STRING,
  };

  if (cross_group < 0 || cross_group >= (int)(sizeof(messages) / sizeof(messages[0]))) {
    return "GRDT_CROSS_GROUP_UNKNOWN";
  }

  const char *msg = messages[cross_group];
  return msg ? msg : "GRDT_CROSS_GROUP_UNKNOWN";
}

grdt_cross_group grdt_cross_group_from_string(const char *cross_group_string, size_t string_size) {
  if (GRDU_STRING_EQUALS(cross_group_string, string_size, GRDT_CROSS_GROUP_LOCAL_STRING)) {
    return GRDT_CROSS_GROUP_LOCAL;
  } else if (GRDU_STRING_EQUALS(cross_group_string, string_size, GRDT_CROSS_GROUP_INBOUND_STRING)) {
    return GRDT_CROSS_GROUP_INBOUND;
  } else if (
      GRDU_STRING_EQUALS(cross_group_string, string_size, GRDT_CROSS_GROUP_OUTBOUND_STRING)) {
    return GRDT_CROSS_GROUP_OUTBOUND;
  } else if (GRDU_STRING_EQUALS(cross_group_string, string_size, GRDT_CROSS_GROUP_CROSS_STRING)) {
    return GRDT_CROSS_GROUP_CROSS;
  }
  return GRDT_CROSS_GROUP_NONE;
}
