#ifndef GRADIDO_BLOCKCHAIN_CORE_TESTS_UNIT_SRC_KEY_PAIRS_H
#define GRADIDO_BLOCKCHAIN_CORE_TESTS_UNIT_SRC_KEY_PAIRS_H

#include <vector>
#include <cstdint>

struct KeyPair {
    uint8_t public_key[32];
    uint8_t private_key[64];
};

extern std::vector<KeyPair> g_KeyPairs;

void init_key_pairs();

#endif // GRADIDO_BLOCKCHAIN_CORE_TESTS_UNIT_SRC_KEY_PAIRS_H
