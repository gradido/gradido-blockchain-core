#include <gtest/gtest.h>

#include "bench_chain_synth.h"

#include "memory_limit.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

/*
 * The generated chain: that a seed gives the same chain every time, that the environment can
 * name another one, and that what comes out still looks like the chain it was measured from.
 * A benchmark run that found something is worth nothing if its chain cannot be built again.
 */

namespace {

const uint8_t kCommunityUuid[ARNM_UUID_BINARY_SIZE] = {0x3a, 0x3d, 0x7b, 0x4c, 0x1f, 0x9e,
                                                       0x4a, 0x61, 0x8d, 0x2b, 0x6c, 0x05,
                                                       0x9f, 0x77, 0xe3, 0x12};

/** What a chain looks like, in numbers that fit in one struct. */
struct Shape {
  uint64_t fingerprint = 1469598103934665603ull; /**< every byte that matters, folded */
  uint32_t count = 0;
  std::array<uint32_t, GRDT_TRANSACTION_COUNT> by_type{};
  std::array<uint32_t, 8> signatures{};
  std::array<uint32_t, 8> balances{};
  std::map<std::array<uint8_t, SIGN_PUBLIC_KEY_SIZE>, uint32_t> involved;
  int64_t first_second = 0;
  int64_t last_second = 0;
};

void Fold(Shape &shape, const uint8_t *bytes, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    shape.fingerprint ^= bytes[i];
    shape.fingerprint *= 1099511628211ull;
  }
}

arnm_result Look(const grdr_complete_transaction *tx, void *context) {
  Shape &shape = *static_cast<Shape *>(context);
  if (!shape.count) { shape.first_second = tx->confirmed_at.seconds; }
  shape.last_second = tx->confirmed_at.seconds;
  ++shape.count;
  shape.by_type[static_cast<size_t>(tx->transaction_type)]++;
  shape.signatures[tx->signature_pairs_count < 7 ? tx->signature_pairs_count : 7]++;
  shape.balances[tx->account_balances_count < 7 ? tx->account_balances_count : 7]++;

  Fold(shape, reinterpret_cast<const uint8_t *>(&tx->tx_nr), sizeof(tx->tx_nr));
  Fold(shape, reinterpret_cast<const uint8_t *>(&tx->confirmed_at.seconds), sizeof(int64_t));
  const uint8_t type = static_cast<uint8_t>(tx->transaction_type);
  Fold(shape, &type, 1);
  for (size_t i = 0; i < tx->signature_pairs_count; ++i) {
    Fold(shape, tx->signature_pairs[i].public_key, SIGN_PUBLIC_KEY_SIZE);
    std::array<uint8_t, SIGN_PUBLIC_KEY_SIZE> key{};
    memcpy(key.data(), tx->signature_pairs[i].public_key, SIGN_PUBLIC_KEY_SIZE);
    shape.involved[key]++;
  }
  for (size_t i = 0; i < tx->account_balances_count; ++i) {
    Fold(shape, tx->account_balances[i].pubkey, SIGN_PUBLIC_KEY_SIZE);
    Fold(shape, tx->account_balances[i].community_uuid, ARNM_UUID_BINARY_SIZE);
    std::array<uint8_t, SIGN_PUBLIC_KEY_SIZE> key{};
    memcpy(key.data(), tx->account_balances[i].pubkey, SIGN_PUBLIC_KEY_SIZE);
    shape.involved[key]++;
  }
  return ARNM_SUCCESS;
}

Shape Walk(uint32_t count, uint64_t seed) {
  bench_chain_source source;
  source.path = nullptr;
  source.count = count;
  source.seed = seed;
  Shape shape;
  uint32_t transactions = 0;
  EXPECT_EQ(
      bench_chain_source_feed(&source, kCommunityUuid, Look, &shape, &transactions), ARNM_SUCCESS
  );
  EXPECT_EQ(transactions, count);
  return shape;
}

TEST(ChainSynth, TheSameSeedGivesTheSameChain) {
  const Shape first = Walk(5000, 12345);
  const Shape again = Walk(5000, 12345);
  EXPECT_EQ(first.fingerprint, again.fingerprint);
  EXPECT_EQ(first.by_type, again.by_type);
  EXPECT_EQ(first.last_second, again.last_second);

  const Shape other = Walk(5000, 12346);
  EXPECT_NE(first.fingerprint, other.fingerprint) << "another seed, another chain";
  // and a chain is a prefix of a longer one with the same seed: the walk has no lookahead
  const Shape longer = Walk(9000, 12345);
  EXPECT_EQ(longer.count, 9000u);
  EXPECT_GT(longer.last_second, first.last_second);
}

TEST(ChainSynth, TheDefaultSeedIsFixedAndTheEnvironmentOverridesIt) {
  const char *before = getenv("GRD_CHAIN_SEED");
  const std::string kept = before ? before : "";
  unsetenv("GRD_CHAIN_SEED");
  EXPECT_EQ(bench_chain_seed(), BENCH_CHAIN_DEFAULT_SEED);

  setenv("GRD_CHAIN_SEED", "4711", 1);
  EXPECT_EQ(bench_chain_seed(), 4711u);
  setenv("GRD_CHAIN_SEED", "NOW", 1);
  const uint64_t now = bench_chain_seed();
  EXPECT_NE(now, BENCH_CHAIN_DEFAULT_SEED);
  EXPECT_NE(now, 4711u);
  setenv("GRD_CHAIN_SEED", "now", 1);
  EXPECT_NE(bench_chain_seed(), BENCH_CHAIN_DEFAULT_SEED) << "the lower case spelling too";

  unsetenv("GRD_CHAIN_SEED");
  EXPECT_EQ(bench_chain_count(), BENCH_CHAIN_DEFAULT_COUNT);
  setenv("GRD_CHAIN_COUNT", "1234", 1);
  EXPECT_EQ(bench_chain_count(), 1234u);
  setenv("GRD_CHAIN_COUNT", "0", 1);
  EXPECT_EQ(bench_chain_count(), BENCH_CHAIN_DEFAULT_COUNT) << "nothing is not a count";
  unsetenv("GRD_CHAIN_COUNT");

  if (!kept.empty()) { setenv("GRD_CHAIN_SEED", kept.c_str(), 1); }
}

TEST(ChainSynth, AFileNamedOnTheCommandLineWins) {
  unsetenv("GRD_BENCH_CHAIN_DATA");
  bench_chain_source generated = bench_chain_source_of(0, nullptr);
  EXPECT_EQ(generated.path, nullptr);
  EXPECT_EQ(generated.count, bench_chain_count());

  char program[] = "test";
  char file[] = "some/chain.dat";
  char *argv[] = {program, file, nullptr};
  const bench_chain_source named = bench_chain_source_of(2, argv);
  ASSERT_NE(named.path, nullptr);
  EXPECT_STREQ(named.path, "some/chain.dat");

  setenv("GRD_BENCH_CHAIN_DATA", "from/the/environment.dat", 1);
  const bench_chain_source from_env = bench_chain_source_of(0, nullptr);
  ASSERT_NE(from_env.path, nullptr);
  EXPECT_STREQ(from_env.path, "from/the/environment.dat");
  unsetenv("GRD_BENCH_CHAIN_DATA");
}

/**
 * The shape the generator exists for. The shares come from `blk00000001.dat`; a percent point
 * of room either way leaves the draw its randomness without letting the chain drift into
 * something else.
 */
TEST(ChainSynth, ItLooksLikeTheChainItWasMeasuredFrom) {
  const Shape shape = Walk(50000, BENCH_CHAIN_DEFAULT_SEED);
  const double count = shape.count;
  auto share = [&](uint32_t n) { return 100.0 * n / count; };

  EXPECT_NEAR(share(shape.by_type[GRDT_TRANSACTION_CREATION]), 58.3, 1.5);
  EXPECT_NEAR(share(shape.by_type[GRDT_TRANSACTION_REGISTER_ADDRESS]), 12.3, 1.5);
  EXPECT_NEAR(share(shape.by_type[GRDT_TRANSACTION_DEFERRED_TRANSFER]), 10.2, 1.5);
  EXPECT_NEAR(share(shape.by_type[GRDT_TRANSACTION_TRANSFER]), 9.1, 1.5);
  EXPECT_NEAR(share(shape.by_type[GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER]), 5.6, 1.5);
  EXPECT_NEAR(share(shape.by_type[GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER]), 4.6, 1.5);
  EXPECT_EQ(shape.by_type[GRDT_TRANSACTION_COMMUNITY_ROOT], 1u) << "a chain has one root";

  EXPECT_NEAR(share(shape.signatures[0]), 5.6, 1.5);
  EXPECT_NEAR(share(shape.signatures[1]), 82.2, 2.0);
  EXPECT_NEAR(share(shape.signatures[3]), 12.2, 1.5);
  EXPECT_NEAR(share(shape.balances[1]), 12.3, 1.5);
  EXPECT_NEAR(share(shape.balances[2]), 25.1, 2.0);
  EXPECT_NEAR(share(shape.balances[3]), 62.7, 2.0);

  // a third as many addresses as transactions, and a handful of them in most of it
  EXPECT_NEAR(static_cast<double>(shape.involved.size()) / count, 0.35, 0.05);
  std::vector<uint32_t> counts;
  for (const auto &entry : shape.involved) { counts.push_back(entry.second); }
  std::sort(counts.begin(), counts.end(), std::greater<uint32_t>());
  uint64_t sum = 0;
  for (uint32_t n : counts) { sum += n; }
  uint64_t top_ten = 0;
  for (size_t i = 0; i < 10 && i < counts.size(); ++i) { top_ten += counts[i]; }
  EXPECT_GT(100.0 * static_cast<double>(top_ten) / static_cast<double>(sum), 45.0)
      << "the busiest addresses carry the chain, as they do in the real one";
  EXPECT_LE(counts[counts.size() / 2], 4u) << "the median address appears a handful of times";

  // days pass at about the rate they do there: one transaction every 69 minutes
  const double seconds = static_cast<double>(shape.last_second - shape.first_second) / count;
  EXPECT_NEAR(seconds, 4150.0, 400.0);
}

} // namespace
