#include "gradido_blockchain_core/types/memo_key.h"
#include "gradido_blockchain_core/utils/string_helper.h"

#include <stdint.h>
#include <string.h>

#define GRDT_MEMO_KEY_SHARED_SECRET_STRING "GRDT_MEMO_KEY_SHARED_SECRET"
#define GRDT_MEMO_KEY_COMMUNITY_SECRET_STRING "GRDT_MEMO_KEY_COMMUNITY_SECRET"
#define GRDT_MEMO_KEY_PLAIN_STRING "GRDT_MEMO_KEY_PLAIN"
#define GRDT_MEMO_KEY_NONE_STRING "GRDT_MEMO_KEY_NONE"

const char *grdt_memo_key_to_string(grdt_memo_key memo_key) {
  static const char *messages[] = {
      [GRDT_MEMO_KEY_SHARED_SECRET] = GRDT_MEMO_KEY_SHARED_SECRET_STRING,
      [GRDT_MEMO_KEY_COMMUNITY_SECRET] = GRDT_MEMO_KEY_COMMUNITY_SECRET_STRING,
      [GRDT_MEMO_KEY_PLAIN] = GRDT_MEMO_KEY_PLAIN_STRING,
      [GRDT_MEMO_KEY_NONE] = GRDT_MEMO_KEY_NONE_STRING,
  };

  if (memo_key < 0 || memo_key >= (int)(sizeof(messages) / sizeof(messages[0]))) {
    return "GRDT_MEMO_KEY_UNKNOWN";
  }

  const char *msg = messages[memo_key];
  return msg ? msg : "GRDT_MEMO_KEY_UNKNOWN";
}

grdt_memo_key grdt_memo_key_from_string(const char *memo_key_string, size_t string_size) {
  if (GRDU_STRING_EQUALS(memo_key_string, string_size, GRDT_MEMO_KEY_SHARED_SECRET_STRING)) {
    return GRDT_MEMO_KEY_SHARED_SECRET;
  } else if (
      GRDU_STRING_EQUALS(memo_key_string, string_size, GRDT_MEMO_KEY_COMMUNITY_SECRET_STRING)) {
    return GRDT_MEMO_KEY_COMMUNITY_SECRET;
  } else if (GRDU_STRING_EQUALS(memo_key_string, string_size, GRDT_MEMO_KEY_PLAIN_STRING)) {
    return GRDT_MEMO_KEY_PLAIN;
  }
  return GRDT_MEMO_KEY_NONE;
}
