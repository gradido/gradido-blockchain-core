#include <gtest/gtest.h>

#include "gradido_blockchain_core/blockchain/chain.h"

#include "bench_chain_data.h"
#include "bench_chain_synth.h"

#include "memory_limit.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

/*
 * The property this file exists for: a chain answers the same however its index is cut.
 *
 * Four chains are filled from one walk of the same transactions -- handed to all four inside a
 * single callback, so the input is identical by construction and not by trusting a seed. Their
 * segment sizes are 1, 7, 64 and the default, which is larger than the whole chain: that last
 * one has a single segment and therefore no merge at all, and is the answer the other three
 * have to reproduce.
 *
 * A wrong merge is a wrong page, and a wrong page is what this catches: pagination applied
 * inside a segment instead of across the row gives the right answer only while every match
 * lives in one segment, which at a segment size of one is never.
 */

namespace {

using Key = std::array<uint8_t, SIGN_PUBLIC_KEY_SIZE>;
using Uuid = std::array<uint8_t, ARNM_UUID_BINARY_SIZE>;

constexpr uint64_t kSeed = 0x5eed1234abcd0001ull;
// A segment reserves about four megabytes of address space for its block pool's first arena,
// whatever it ends up holding -- nothing in production, where a segment is millions of
// transactions, but at a segment size of one it is four megabytes per transaction. Four chains
// over this many transactions come to some 370 segments and stay well inside the 2048 MB cap
// that memory_limit.h puts on a test binary. It is plenty: at a segment size of one, a page of
// 32 crosses 32 segment borders.
constexpr uint32_t kTransactions = 320;
constexpr uint32_t kPage = 32;

const uint8_t kCommunityUuid[ARNM_UUID_BINARY_SIZE] = {0x3a, 0x3d, 0x7b, 0x4c, 0x1f, 0x9e,
                                                       0x4a, 0x61, 0x8d, 0x2b, 0x6c, 0x05,
                                                       0x9f, 0x77, 0xe3, 0x12};

/** What one call answered, compared field by field between chains. */
struct Answer {
  arnm_result result = ARNM_SUCCESS;
  uint64_t count = 0;
  uint32_t written = 0;
  std::vector<uint64_t> page;
  bool has_newest = false;
  uint64_t newest = 0;

  bool operator==(const Answer &other) const {
    return result == other.result && count == other.count && written == other.written &&
           page == other.page && has_newest == other.has_newest &&
           (!has_newest || newest == other.newest);
  }
};

/** Everything the walk collects, so the filters below have real keys and dates to name. */
struct Harvest {
  std::vector<grdb_chain *> chains;
  std::vector<Key> keys;     /**< signers and balance holders, in order of first sight */
  std::vector<Uuid> foreign; /**< coin communities that are not the chain's own */
  std::set<grdt_transaction> types;
  int64_t first_second = 0;
  int64_t last_second = 0;
  uint64_t first_tx = 0;
  uint64_t last_tx = 0;
  uint32_t seen = 0;
  arnm_result failure = ARNM_SUCCESS;
};

void Remember(Harvest *harvest, const uint8_t *key) {
  if (harvest->keys.size() >= 64) { return; }
  Key copy{};
  std::memcpy(copy.data(), key, SIGN_PUBLIC_KEY_SIZE);
  for (const Key &known : harvest->keys) {
    if (known == copy) { return; }
  }
  harvest->keys.push_back(copy);
}

arnm_result Feed(const grdr_complete_transaction *tx, void *context) {
  auto *harvest = static_cast<Harvest *>(context);

  for (grdb_chain *chain : harvest->chains) {
    const arnm_result result = grdb_chain_add(chain, tx);
    if (ARNM_SUCCESS != result) {
      harvest->failure = result;
      return result;
    }
  }

  if (!harvest->seen) {
    harvest->first_second = tx->confirmed_at.seconds;
    harvest->first_tx = tx->tx_nr;
  }
  harvest->last_second = tx->confirmed_at.seconds;
  harvest->last_tx = tx->tx_nr;
  ++harvest->seen;
  harvest->types.insert(tx->transaction_type);

  for (size_t i = 0; i < tx->signature_pairs_count; ++i) {
    Remember(harvest, tx->signature_pairs[i].public_key);
  }
  for (size_t i = 0; i < tx->account_balances_count; ++i) {
    Remember(harvest, tx->account_balances[i].pubkey);
    Uuid coin{};
    std::memcpy(coin.data(), tx->account_balances[i].community_uuid, ARNM_UUID_BINARY_SIZE);
    if (0 != std::memcmp(coin.data(), kCommunityUuid, ARNM_UUID_BINARY_SIZE)) {
      bool known = false;
      for (const Uuid &seen : harvest->foreign) { known = known || seen == coin; }
      if (!known && harvest->foreign.size() < 8) { harvest->foreign.push_back(coin); }
    }
  }
  return ARNM_SUCCESS;
}

/** One chain asked one way. */
Answer Ask(
    const grdb_chain *chain,
    const grdb_transactions_filter &filter,
    uint32_t skip,
    uint32_t size,
    bool descending
) {
  Answer answer;
  std::vector<uint64_t> room(size ? size : 1u, 0u);
  answer.result = grdb_chain_listing(
      chain, &filter, skip, size, descending, size ? room.data() : nullptr, &answer.written,
      &answer.count
  );
  if (ARNM_SUCCESS == answer.result) {
    answer.page.assign(room.begin(), room.begin() + answer.written);
  }

  uint64_t separate_count = 0;
  const arnm_result counted = grdb_chain_count(chain, &filter, &separate_count);
  EXPECT_EQ(ARNM_SUCCESS, counted);
  // the count of a listing and the count of a count are the same question
  if (ARNM_SUCCESS == answer.result && ARNM_SUCCESS == counted) {
    EXPECT_EQ(answer.count, separate_count);
  }

  answer.has_newest = grdb_chain_newest(chain, &filter, &answer.newest);
  return answer;
}

std::string Describe(const Answer &answer) {
  std::string text = "count=" + std::to_string(answer.count) +
                     " written=" + std::to_string(answer.written) + " page=[";
  for (size_t i = 0; i < answer.page.size(); ++i) {
    text += (i ? "," : "") + std::to_string(answer.page[i]);
  }
  text += "] newest=";
  text += answer.has_newest ? std::to_string(answer.newest) : std::string("none");
  return text;
}

/** The four chains of one fixture, filled together and released together. */
class ChainEquivalence : public ::testing::Test {
protected:
  static constexpr uint32_t kSizes[4] = {1u, 7u, 64u, GRDB_SEGMENT_TRANSACTIONS_DEFAULT};

  void SetUp() override {
    for (uint32_t i = 0; i < 4; ++i) {
      grdb_chain_options options = {};
      options.segment_transactions = kSizes[i];
      ASSERT_EQ(ARNM_SUCCESS, grdb_chain_init(&chains_[i], &options, nullptr));
      harvest_.chains.push_back(&chains_[i]);
    }

    const bench_chain_source source = {nullptr, kTransactions, kSeed};
    uint32_t handed = 0;
    ASSERT_EQ(
        ARNM_SUCCESS, bench_chain_source_feed(&source, kCommunityUuid, Feed, &harvest_, &handed)
    );
    ASSERT_EQ(ARNM_SUCCESS, harvest_.failure);
    ASSERT_EQ(kTransactions, handed);
    ASSERT_FALSE(harvest_.keys.empty());
  }

  void TearDown() override {
    for (grdb_chain &chain : chains_) { grdb_chain_release(&chain); }
  }

  /** The chain with one segment: no merge runs in it, so it is the answer to reproduce. */
  const grdb_chain *reference() const {
    return &chains_[3];
  }

  /** Asks every chain and fails naming the segment size that disagreed. */
  void ExpectSame(
      const grdb_transactions_filter &filter,
      uint32_t skip,
      uint32_t size,
      bool descending,
      const std::string &what
  ) {
    const Answer expected = Ask(reference(), filter, skip, size, descending);
    for (uint32_t i = 0; i < 3; ++i) {
      const Answer actual = Ask(&chains_[i], filter, skip, size, descending);
      EXPECT_TRUE(expected == actual)
          << what << ", skip " << skip << ", size " << size
          << (descending ? ", descending" : ", ascending") << "\n  segment size " << kSizes[i]
          << ": " << Describe(actual) << "\n  segment size " << kSizes[3] << ": "
          << Describe(expected);
    }
  }

  /** The skips worth trying: the first page, one that crosses segments, one past the end. */
  void ExpectSameEverywhere(const grdb_transactions_filter &filter, const std::string &what) {
    uint64_t total = 0;
    ASSERT_EQ(ARNM_SUCCESS, grdb_chain_count(reference(), &filter, &total));
    const std::vector<uint32_t> skips = {
        0u,
        1u,
        kPage,
        total > kPage ? static_cast<uint32_t>(total / 2u) : 0u,
        total ? static_cast<uint32_t>(total - 1u) : 0u,
        static_cast<uint32_t>(total + 5u)
    };
    for (uint32_t skip : skips) {
      ExpectSame(filter, skip, kPage, true, what);
      ExpectSame(filter, skip, kPage, false, what);
    }
    ExpectSame(filter, 0, 0, true, what + " (count only)");
    ExpectSame(filter, 0, 1, true, what + " (single)");
  }

  grdb_chain chains_[4]{};
  Harvest harvest_;
};

constexpr uint32_t ChainEquivalence::kSizes[4];

TEST_F(ChainEquivalence, SegmentsAreCutWhereTheSizeSaysAndTheRowIsGapless) {
  for (uint32_t i = 0; i < 4; ++i) {
    const uint64_t first = harvest_.first_tx / kSizes[i];
    const uint64_t last = harvest_.last_tx / kSizes[i];
    EXPECT_EQ(last - first + 1u, grdb_chain_segment_count(&chains_[i]))
        << "segment size " << kSizes[i];
    EXPECT_EQ(kTransactions, grdb_chain_size(&chains_[i]));
  }
  // the point of the fixture: one of them really does run without a merge, the others really do
  EXPECT_EQ(1u, grdb_chain_segment_count(reference()));
  EXPECT_EQ(kTransactions, grdb_chain_segment_count(&chains_[0]));
}

TEST_F(ChainEquivalence, EveryTransaction) {
  const grdb_transactions_filter filter = {};
  ExpectSameEverywhere(filter, "no filter");
}

TEST_F(ChainEquivalence, ByTransactionType) {
  for (grdt_transaction type : harvest_.types) {
    grdb_transactions_filter filter = {};
    ASSERT_EQ(ARNM_SUCCESS, grdb_transactions_filter_set_transaction_type(&filter, type));
    ExpectSameEverywhere(filter, std::string("type ") + grdt_transaction_to_string(type));
  }
}

TEST_F(ChainEquivalence, ByAddressInEachRole) {
  const size_t sample = harvest_.keys.size() < 6 ? harvest_.keys.size() : 6;
  for (size_t i = 0; i < sample; ++i) {
    for (grdb_address_role role : {GRDB_ADDRESS_ROLE_INVOLVED, GRDB_ADDRESS_ROLE_BALANCE}) {
      grdb_transactions_filter filter = {};
      ASSERT_EQ(
          ARNM_SUCCESS, grdb_transactions_filter_set_address(&filter, harvest_.keys[i].data(), role)
      );
      ExpectSameEverywhere(
          filter, "address " + std::to_string(i) + " role " + std::to_string((int)role)
      );
    }
  }
}

TEST_F(ChainEquivalence, ByAddressAndType) {
  grdb_transactions_filter filter = {};
  ASSERT_EQ(
      ARNM_SUCCESS, grdb_transactions_filter_set_address(
                        &filter, harvest_.keys[0].data(), GRDB_ADDRESS_ROLE_INVOLVED
                    )
  );
  ASSERT_EQ(
      ARNM_SUCCESS,
      grdb_transactions_filter_set_transaction_type(&filter, GRDT_TRANSACTION_TRANSFER)
  );
  ExpectSameEverywhere(filter, "address 0 and transfer");
}

TEST_F(ChainEquivalence, ByTransactionRangeAcrossSegmentBorders) {
  // a range that no segment size lines up with, so every cut has to split it differently
  const std::vector<std::pair<uint64_t, uint64_t>> ranges = {
      {harvest_.first_tx, harvest_.last_tx},
      {harvest_.first_tx + 3u, harvest_.first_tx + 3u},
      {harvest_.first_tx + 5u, harvest_.first_tx + 130u},
      {harvest_.last_tx / 2u, harvest_.last_tx},
      {harvest_.last_tx + 10u, harvest_.last_tx + 20u}
  };
  for (const auto &range : ranges) {
    grdb_transactions_filter filter = {};
    ASSERT_EQ(
        ARNM_SUCCESS, grdb_transactions_filter_set_tx_range(&filter, range.first, range.second)
    );
    ExpectSameEverywhere(
        filter, "range " + std::to_string(range.first) + ".." + std::to_string(range.second)
    );
  }
}

TEST_F(ChainEquivalence, ByDateRange) {
  const int64_t span = harvest_.last_second - harvest_.first_second;
  const std::vector<std::pair<int64_t, int64_t>> windows = {
      {harvest_.first_second, harvest_.last_second},
      {harvest_.first_second, harvest_.first_second + span / 4},
      {harvest_.first_second + span / 3, harvest_.first_second + 2 * span / 3},
      {harvest_.first_second + span / 2, harvest_.last_second},
      {harvest_.last_second + 86400, harvest_.last_second + 2 * 86400}
  };
  for (const auto &window : windows) {
    grdb_transactions_filter filter = {};
    ASSERT_EQ(
        ARNM_SUCCESS, grdb_transactions_filter_set_date_range(&filter, window.first, window.second)
    );
    ExpectSameEverywhere(filter, "days " + std::to_string(window.first));
  }
}

TEST_F(ChainEquivalence, ByCoinCommunity) {
  grdb_transactions_filter own = {};
  ASSERT_EQ(ARNM_SUCCESS, grdb_transactions_filter_set_coin_community(&own, kCommunityUuid));
  ExpectSameEverywhere(own, "own coin only");

  for (size_t i = 0; i < harvest_.foreign.size(); ++i) {
    grdb_transactions_filter filter = {};
    ASSERT_EQ(
        ARNM_SUCCESS,
        grdb_transactions_filter_set_coin_community(&filter, harvest_.foreign[i].data())
    );
    ExpectSameEverywhere(filter, "foreign coin " + std::to_string(i));
  }
}

TEST_F(ChainEquivalence, RangeOfDaysIsTheSameSpan) {
  const int64_t span = harvest_.last_second - harvest_.first_second;
  for (int64_t step = 0; step <= 4; ++step) {
    const int64_t from = harvest_.first_second + step * span / 5;
    const int64_t to = harvest_.first_second + (step + 1) * span / 5;
    uint64_t expected_min = 0, expected_max = 0;
    const bool expected =
        grdb_chain_range_of_days(reference(), from, to, &expected_min, &expected_max);
    for (uint32_t i = 0; i < 3; ++i) {
      uint64_t actual_min = 0, actual_max = 0;
      const bool actual = grdb_chain_range_of_days(&chains_[i], from, to, &actual_min, &actual_max);
      EXPECT_EQ(expected, actual) << "segment size " << kSizes[i];
      if (expected && actual) {
        EXPECT_EQ(expected_min, actual_min) << "segment size " << kSizes[i];
        EXPECT_EQ(expected_max, actual_max) << "segment size " << kSizes[i];
      }
    }
  }
}

TEST_F(ChainEquivalence, TheAddressRegisterIsNotSegmentedAndSaysTheSame) {
  for (const Key &key : harvest_.keys) {
    const grdt_address expected = grdb_chain_address_type(reference(), key.data());
    uint64_t expected_balance = 0;
    const bool had_balance =
        grdb_chain_address_last_balance(reference(), key.data(), &expected_balance);
    for (uint32_t i = 0; i < 3; ++i) {
      EXPECT_EQ(expected, grdb_chain_address_type(&chains_[i], key.data()));
      uint64_t balance = 0;
      EXPECT_EQ(had_balance, grdb_chain_address_last_balance(&chains_[i], key.data(), &balance));
      if (had_balance) { EXPECT_EQ(expected_balance, balance); }
      grdt_address at = GRDT_ADDRESS_NONE;
      grdt_address reference_at = GRDT_ADDRESS_NONE;
      const uint64_t middle = harvest_.last_tx / 2u;
      EXPECT_EQ(
          grdb_chain_address_type_at(reference(), key.data(), middle, &reference_at),
          grdb_chain_address_type_at(&chains_[i], key.data(), middle, &at)
      );
      EXPECT_EQ(reference_at, at);
    }
  }
}

// ********** the chain itself, apart from the merge *******************

TEST(Chain, EmptyAnswersNothingWithoutReadingASet) {
  grdb_chain chain;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_init(&chain, nullptr, nullptr));

  const grdb_transactions_filter filter = {};
  uint64_t count = 7;
  EXPECT_EQ(ARNM_SUCCESS, grdb_chain_count(&chain, &filter, &count));
  EXPECT_EQ(0u, count);

  uint64_t page[4] = {0};
  uint32_t written = 9;
  uint64_t listed = 7;
  EXPECT_EQ(ARNM_SUCCESS, grdb_chain_listing(&chain, &filter, 0, 4, true, page, &written, &listed));
  EXPECT_EQ(0u, written);
  EXPECT_EQ(0u, listed);

  uint64_t newest = 0;
  EXPECT_FALSE(grdb_chain_newest(&chain, &filter, &newest));
  EXPECT_EQ(0u, grdb_chain_segment_count(&chain));
  EXPECT_EQ(0u, grdb_chain_size(&chain));
  EXPECT_EQ(nullptr, grdb_chain_segment(&chain, 0));

  grdb_chain_release(&chain);
  EXPECT_FALSE(chain.ready);
}

TEST(Chain, RefusesANumberThatIsNotTheNextOne) {
  grdb_chain chain;
  grdb_chain_options options = {};
  options.segment_transactions = 4;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_init(&chain, &options, nullptr));

  const bench_chain_source source = {nullptr, 12, kSeed};
  Harvest harvest;
  harvest.chains.push_back(&chain);
  uint32_t handed = 0;
  ASSERT_EQ(
      ARNM_SUCCESS, bench_chain_source_feed(&source, kCommunityUuid, Feed, &harvest, &handed)
  );
  ASSERT_EQ(12u, handed);

  grdr_complete_transaction skipped;
  grdr_complete_transaction_init(&skipped);
  skipped.tx_nr = harvest.last_tx + 2u;
  skipped.transaction_type = GRDT_TRANSACTION_TRANSFER;
  skipped.confirmed_at.seconds = harvest.last_second + 60;
  std::memcpy(skipped.tx_community_uuid, kCommunityUuid, ARNM_UUID_BINARY_SIZE);
  EXPECT_EQ(ARNM_ERROR_INVALID_PARAM, grdb_chain_add(&chain, &skipped));

  skipped.tx_nr = 0;
  EXPECT_EQ(ARNM_ERROR_INVALID_PARAM, grdb_chain_add(&chain, &skipped));

  EXPECT_EQ(12u, grdb_chain_size(&chain));
  grdb_chain_release(&chain);
}

TEST(Chain, ResetEmptiesItAndItFillsAgain) {
  grdb_chain chain;
  grdb_chain_options options = {};
  options.segment_transactions = 8;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_init(&chain, &options, nullptr));

  const bench_chain_source source = {nullptr, 40, kSeed};
  for (int round = 0; round < 2; ++round) {
    Harvest harvest;
    harvest.chains.push_back(&chain);
    uint32_t handed = 0;
    ASSERT_EQ(
        ARNM_SUCCESS, bench_chain_source_feed(&source, kCommunityUuid, Feed, &harvest, &handed)
    );
    ASSERT_EQ(ARNM_SUCCESS, harvest.failure);
    EXPECT_EQ(40u, grdb_chain_size(&chain));

    const grdb_transactions_filter filter = {};
    uint64_t count = 0;
    EXPECT_EQ(ARNM_SUCCESS, grdb_chain_count(&chain, &filter, &count));
    EXPECT_EQ(40u, count);

    grdb_chain_reset(&chain);
    EXPECT_EQ(0u, grdb_chain_size(&chain));
    EXPECT_EQ(0u, grdb_chain_segment_count(&chain));
    EXPECT_TRUE(chain.ready);
  }
  grdb_chain_release(&chain);
}

TEST(Chain, RefusesAPageLargerThanOne) {
  grdb_chain chain;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_init(&chain, nullptr, nullptr));
  const grdb_transactions_filter filter = {};
  std::vector<uint64_t> room(GRDB_PAGE_MAX + 1u, 0u);
  uint32_t written = 0;
  uint64_t count = 0;
  EXPECT_EQ(
      ARNM_ERROR_INVALID_PARAM,
      grdb_chain_listing(
          &chain, &filter, 0, GRDB_PAGE_MAX + 1u, true, room.data(), &written, &count
      )
  );
  grdb_chain_release(&chain);
}

TEST(Chain, NullArgumentsAreRefusedNotDereferenced) {
  const grdb_transactions_filter filter = {};
  uint64_t out = 0;
  uint32_t written = 0;
  EXPECT_EQ(ARNM_ERROR_NULL_POINTER, grdb_chain_init(nullptr, nullptr, nullptr));
  EXPECT_EQ(ARNM_ERROR_NULL_POINTER, grdb_chain_count(nullptr, &filter, &out));
  EXPECT_EQ(ARNM_ERROR_NULL_POINTER, grdb_chain_add(nullptr, nullptr));
  EXPECT_EQ(
      ARNM_ERROR_NULL_POINTER,
      grdb_chain_listing(nullptr, &filter, 0, 0, true, nullptr, &written, &out)
  );
  EXPECT_FALSE(grdb_chain_newest(nullptr, &filter, &out));
  EXPECT_FALSE(grdb_chain_range_of_days(nullptr, 0, 0, &out, &out));
  EXPECT_EQ(GRDT_ADDRESS_NONE, grdb_chain_address_type(nullptr, nullptr));
  EXPECT_FALSE(grdb_chain_knows_address(nullptr, nullptr));
  EXPECT_EQ(0u, grdb_chain_segment_count(nullptr));
  EXPECT_EQ(nullptr, grdb_chain_segment(nullptr, 0));
  grdb_chain_release(nullptr);
  grdb_chain_reset(nullptr);
}

} // namespace
