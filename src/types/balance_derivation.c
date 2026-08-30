#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_blockchain_core/utils/string_helper.h"

#include <stdint.h>
#include <string.h>

#define GRDT_BALANCE_DERIVATION_UNSPECIFIED_STRING "GRDT_BALANCE_DERIVATION_UNSPECIFIED"
#define GRDT_BALANCE_DERIVATION_NODE_STRING "GRDT_BALANCE_DERIVATION_NODE"
#define GRDT_BALANCE_DERIVATION_EXTERN_STRING "GRDT_BALANCE_DERIVATION_EXTERN"

const char *grdt_balance_derivation_to_string(grdt_balance_derivation balance_derivation) {
  static const char *messages[] = {
      [GRDT_BALANCE_DERIVATION_UNSPECIFIED] = GRDT_BALANCE_DERIVATION_UNSPECIFIED_STRING,
      [GRDT_BALANCE_DERIVATION_NODE] = GRDT_BALANCE_DERIVATION_NODE_STRING,
      [GRDT_BALANCE_DERIVATION_EXTERN] = GRDT_BALANCE_DERIVATION_EXTERN_STRING,
  };

  if (balance_derivation < 0 ||
      balance_derivation >= (int)(sizeof(messages) / sizeof(messages[0]))) {
    return "GRDT_BALANCE_UNKNOWN";
  }

  const char *msg = messages[balance_derivation];
  return msg ? msg : "GRDT_BALANCE_UNKNOWN";
}

grdt_balance_derivation grdt_balance_derivation_from_string(
    const char *balance_derivation_string, size_t string_size
) {
  if (GRDU_STRING_EQUALS(
          balance_derivation_string, string_size, GRDT_BALANCE_DERIVATION_NODE_STRING
      )) {
    return GRDT_BALANCE_DERIVATION_NODE;
  } else if (
      GRDU_STRING_EQUALS(
          balance_derivation_string, string_size, GRDT_BALANCE_DERIVATION_EXTERN_STRING
      )) {
    return GRDT_BALANCE_DERIVATION_EXTERN;
  }
  return GRDT_BALANCE_DERIVATION_UNSPECIFIED;
}
