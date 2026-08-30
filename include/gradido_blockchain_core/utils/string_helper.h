#ifndef GRADIDO_BLOCKCHAIN_CORE_UTILS_STRING_HELPER_H
#define GRADIDO_BLOCKCHAIN_CORE_UTILS_STRING_HELPER_H

#ifdef __cplusplus
extern "C" {
#endif

// helper for working with const strings

#define GRDU_STRING_LEN(str) ((uint32_t)(sizeof(str) - 1u))
// use only const strings like const char* str = "string"  oder #define STRING "string" for third
// parameter want
#define GRDU_STRING_EQUALS(str, str_size, want)                                                    \
  ((str_size) == GRDU_STRING_LEN(want) && 0 == memcmp((str), (want), sizeof(want) - 1u))

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_UTILS_STRING_HELPER_H
