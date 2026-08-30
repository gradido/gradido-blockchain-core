#ifndef GRADIDO_BLOCKCHAIN_CORE_TYPES_MEMO_KEY_TYPE_H
#define GRADIDO_BLOCKCHAIN_CORE_TYPES_MEMO_KEY_TYPE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GRDT_MEMO_KEY_SHARED_SECRET = 0,
  GRDT_MEMO_KEY_COMMUNITY_SECRET = 1,
  GRDT_MEMO_KEY_PLAIN = 2,
  /* as error value, on this place, because 0 - 2 already pinned to protobuf enum values */
  GRDT_MEMO_KEY_NONE = 3
} grdt_memo_key;

const char *grdt_memo_key_to_string(grdt_memo_key memo_key);
grdt_memo_key grdt_memo_key_from_string(const char *memo_key_string, size_t string_size);

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_TYPES_MEMO_KEY_TYPE_H
