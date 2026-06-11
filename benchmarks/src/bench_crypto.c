#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/crypto/sign.h"
#include "gradido_blockchain_core/utils/mono_timer.h"

#include <sodium.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define TEST_SEEDS_COUNT 20
#define STRING_BUFFER_SIZE 32
char benchBuffer[STRING_BUFFER_SIZE];

const char* test_seed_strings[] = {
    "A medium of exchange for the people",
    "Love and health",
    "Three cheers",
    "We make the Earth a paradise for all beings",
    "Gratitude balance",
    "Follows its call",
    "The present is theirs; the future, for which I have really worked, is mine. - Nikola Tesla",
    "Imagination is more important than knowledge. - Albert Einstein",
    "I have not failed. I've just found 10,000 ways that won't work. - Thomas Edison",
    "The only way to do great work is to love what you do. - Steve Jobs",
    "The true sign of intelligence is not knowledge but imagination. - Albert Einstein",
    "If you want to find the secrets of the universe, think in terms of energy, frequency, and "
    "vibration. - Nikola Tesla",
    "Success is not final, failure is not fatal: It is the courage to continue that counts. - "
    "Winston Churchill",
    "To invent, you need a good imagination and a pile of junk. - Thomas Edison",
    "In the middle of every difficulty lies opportunity. - Albert Einstein",
    "Our virtues and our failings are inseparable, like force and matter. When they separate, "
    "man is no more. - Nikola Tesla",
    "The important thing is not to stop questioning. Curiosity has its own reason for existence. "
    "- Albert Einstein",
    "The scientists of today think deeply instead of clearly. One must be sane to think clearly, "
    "but one can think deeply and be quite insane. - Nikola Tesla",
    "Life is like riding a bicycle. To keep your balance, you must keep moving. - Albert "
    "Einstein",
    "Genius is one percent inspiration, ninety-nine percent perspiration. - Thomas Edison"
};
uint8_t test_seeds[TEST_SEEDS_COUNT][SIGN_SEED_SIZE];

static const uint8_t* getNextTestValue() {
  static int cursor = 0;
  const uint8_t* result = test_seeds[cursor++];
  if (cursor >= TEST_SEEDS_COUNT) {
    cursor = 0;
  }
  return result;
}


static void test_full_key_derivation(int stepCount)
{
    grdc_sign_key_pair key_pair;
    for (int i = 0; i < stepCount; ++i) {
      grdc_sign_key_pair_derive_account_from_community(&key_pair, getNextTestValue(), getNextTestValue(), 1);
    }
}

static void test_user_key_derivation(int stepCount)
{
    grdc_sign_key_pair root_key_pair;
    grdc_sign_key_pair key_pair;
    grdc_sign_key_pair_generate_from_seed(&root_key_pair, getNextTestValue(), SIGN_SEED_SIZE);
    for (int i = 0; i < stepCount; ++i) {
      grdc_sign_key_pair_derive_uuid(&key_pair, &root_key_pair, getNextTestValue());
    }
}

static void test_account_key_derivation(int stepCount)
{
    grdc_sign_key_pair root_key_pair;
    grdc_sign_key_pair key_pair;
    grdc_sign_key_pair_generate_from_seed(&root_key_pair, getNextTestValue(), SIGN_SEED_SIZE);
    for (int i = 0; i < stepCount; ++i) {
      grdc_sign_key_pair_derive_uuid(&key_pair, &root_key_pair, getNextTestValue());
      grdc_sign_key_pair_derive(&key_pair, &key_pair, 0x80000000 + 1);
    }
}

static void prepare_test_data()
{
  srand(12812);
  for (int i = 0; i < TEST_SEEDS_COUNT; ++i) {
    crypto_generichash(
      test_seeds[i], SIGN_SEED_SIZE, (const unsigned char *)test_seed_strings[i], strlen(test_seed_strings[i]), NULL,
        0
    );
  }
}

static void bench_step(void (*func_ptr)(int), int stepCount, const char* name)
{
  char buffer[STRING_BUFFER_SIZE*2];
  grdu_mono_timer timeUsed;
  grdu_mono_timer_reset(&timeUsed);
  func_ptr(stepCount);
  grdu_mono_timer_string(buffer, STRING_BUFFER_SIZE*2, timeUsed);
  printf("%s: %s\n", name, buffer);
}

int main(void)
{
  char buffer[STRING_BUFFER_SIZE];
  grdu_mono_timer_init();
  grdu_mono_timer timeUsed;
  prepare_test_data();
  grdu_mono_timer_reset(&timeUsed);
  grdu_mono_timer_string(buffer, STRING_BUFFER_SIZE, timeUsed);
  printf("time for prepare test data: %s\n", buffer);

  const int stepCount = 1000;

  bench_step(test_full_key_derivation, stepCount, "loop: seed -> community root key -> user public key (4 steps) -> account public key (1)");
  bench_step(test_user_key_derivation, stepCount, "seed -> community root key, loop: community root key -> user public key (4 steps)");
  bench_step(test_account_key_derivation, stepCount, "seed -> community root key, loop: community root key -> user public key (4 steps) -> account public key (1)");

  grdu_mono_timer_string(buffer, STRING_BUFFER_SIZE, timeUsed);
  printf("all benchmarks: %s, stepSize: %d\n", buffer, stepCount);

  return 0;
}
