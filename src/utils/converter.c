#include "gradido_blockchain_core/utils/converter.h"


size_t grdu_uint64_to_string_size(uint64_t value)
{
  static uint64_t powers[] = {
    10, 100, 1000, 10000, 100000,
    1000000, 10000000, 100000000,
    1000000000, 10000000000, 100000000000,
    1000000000000, 10000000000000, 100000000000000,
    1000000000000000, 10000000000000000, 100000000000000000,
    1000000000000000000, 10000000000000000000u
  };
  int i = 0;
  while (value >= powers[i] && i < 18) {
    ++i;
  }
  return i + 1;
}

size_t grdu_uint64_to_string_known_string_size(char* buffer, uint64_t value, size_t stringSize)
{
  if (value == 0) {
    if (stringSize < 1) {
      return 1; // return required size without null terminator
    }
    buffer[0] = '0';
    buffer[1] = '\0';
    return 1;
  }
  uint64_t temp = value;
  int len = stringSize;
  int cursor = len;
  buffer[cursor] = '\0';

  static const char DIGIT_TABLE[201] =
    "00010203040506070809"
    "10111213141516171819"
    "20212223242526272829"
    "30313233343536373839"
    "40414243444546474849"
    "50515253545556575859"
    "60616263646566676869"
    "70717273747576777879"
    "80818283848586878889"
    "90919293949596979899";

  // process 2 digits at a time
  while (temp >= 100) {
    if (cursor < 2) {
      return grdu_uint64_to_string_size(value); // return required size without null terminator
    }
    uint64_t q = temp / 100;
    uint64_t r = temp - q * 100;
    buffer[--cursor] = DIGIT_TABLE[r * 2 + 1];
    buffer[--cursor] = DIGIT_TABLE[r * 2];
    temp = q;
  }

  // last 1 or 2 digits
  if (temp < 10) {
    if (cursor < 1) {
      return grdu_uint64_to_string_size(value); // return required size without null terminator
    }
    buffer[--cursor] = '0' + (char)temp;
  }
  else {
    if (cursor < 2) {
      return grdu_uint64_to_string_size(value); // return required size without null terminator
    }
    buffer[--cursor] = DIGIT_TABLE[temp * 2 + 1];
    buffer[--cursor] = DIGIT_TABLE[temp * 2];
  }
  return len; // return number of characters written, not counting null terminator
}

// for easy use, one call

size_t grdu_uint64_to_string(char* buffer, size_t bufferSize, uint64_t value)
{
  size_t requiredSize = grdu_uint64_to_string_size(value);
  if (bufferSize < requiredSize + 1) {
    // better safe then sorry
    if (bufferSize) {
      buffer[0] = '\0';
    }
    return requiredSize; // return required size without null terminator
  }
  return grdu_uint64_to_string_known_string_size(buffer, value, requiredSize);
}

