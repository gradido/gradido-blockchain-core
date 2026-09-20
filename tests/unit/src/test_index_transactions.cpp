#include <gtest/gtest.h>

#include "gradido_blockchain_core/index/transactions.h"

#include "bench_chain_data.h"
#include "bench_chain_synth.h"

#include "memory_limit.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

/*
 * grdx_transactions against a reference that answers the same questions by walking a
 * vector of transactions. The index is built from the same transactions, so a disagreement is
 * the index's: the reference knows nothing of sets, keys or days, it compares fields.
 *
 * Synthetic transactions carry the shapes a chain has -- an address that only signs, one whose
 * balance moves, one named in a body, foreign coins, several transactions a day and days with
 * none -- and a real chain file is used on top when one is there.
 *
 * Chain file: $GRD_BENCH_CHAIN_DATA, else ../gradido_blockchain/build/tests/data/blk00000001.dat
 * from the working directory. That case is skipped when neither exists.
 */

namespace {

using Key = std::array<uint8_t, SIGN_PUBLIC_KEY_SIZE>;
using Uuid = std::array<uint8_t, ARNM_UUID_BINARY_SIZE>;

/** What the reference remembers of one transaction: enough to answer any filter. */
struct Record {
  uint64_t tx_nr = 0;
  int64_t seconds = 0;
  grdt_transaction type = GRDT_TRANSACTION_NONE;
  std::set<Key> balance;  /**< balance changed */
  std::set<Key> signer;   /**< signed */
  std::set<Key> other;    /**< named without either */
  std::set<Uuid> foreign; /**< coins that are not the chain's own */
};

bool Involves(const Record &record, const Key &key, grdx_address_role role) {
  if (GRDX_ADDRESS_ROLE_BALANCE == role) { return record.balance.count(key) != 0; }
  return record.balance.count(key) || record.signer.count(key) || record.other.count(key);
}

/** The reference answer: the transaction numbers a filter matches, ascending. */
std::vector<uint64_t> Matching(
    const std::vector<Record> &records,
    const Uuid &chain_uuid,
    const Key *key,
    grdx_address_role role,
    grdt_transaction type,
    const Uuid *coin,
    uint64_t min_tx,
    uint64_t max_tx,
    int64_t from_seconds,
    int64_t to_seconds
) {
  std::vector<uint64_t> matches;
  for (const Record &record : records) {
    if (min_tx && record.tx_nr < min_tx) { continue; }
    if (max_tx && record.tx_nr > max_tx) { continue; }
    if (from_seconds || to_seconds) {
      // the index answers days, not seconds: a day the span touches counts whole
      const int64_t day = record.seconds / 86400;
      if (from_seconds && day < from_seconds / 86400) { continue; }
      if (to_seconds && day > to_seconds / 86400) { continue; }
    }
    if (GRDX_ADDRESS_ROLE_NONE != role && (!key || !Involves(record, *key, role))) { continue; }
    if (GRDT_TRANSACTION_NONE != type && record.type != type) { continue; }
    if (coin) {
      if (*coin == chain_uuid) {
        if (!record.foreign.empty()) { continue; }
      } else if (!record.foreign.count(*coin)) {
        continue;
      }
    }
    matches.push_back(record.tx_nr);
  }
  return matches;
}

/** A transaction built for the index, with the arrays it points at kept beside it. */
struct Transaction {
  grdr_complete_transaction tx{};
  std::vector<grdw_signature_pair> signatures;
  std::vector<grdw_account_balance> balances;
};

Key MakeKey(uint32_t n) {
  Key key{};
  for (size_t i = 0; i < key.size(); ++i) { key[i] = static_cast<uint8_t>(n * 31u + i * 7u + 1u); }
  return key;
}

Uuid MakeUuid(uint8_t n) {
  Uuid uuid{};
  uuid[0] = static_cast<uint8_t>(n + 1u);
  uuid[15] = static_cast<uint8_t>(n * 3u + 2u);
  return uuid;
}

class Index {
public:
  explicit Index(const grdx_transactions_options *options = nullptr) {
    EXPECT_EQ(grdx_transactions_init(&index, options, nullptr), ARNM_SUCCESS);
  }
  ~Index() {
    grdx_transactions_release(&index);
  }
  Index(const Index &) = delete;
  Index &operator=(const Index &) = delete;
  grdx_transactions index{};
};

/** Everything the index matches, read out in pages, so a disagreement can be named. */
std::vector<uint64_t> AllMatches(
    const grdx_transactions *index, const grdx_transactions_filter &filter
) {
  std::vector<uint64_t> all;
  uint32_t skip = 0;
  for (;;) {
    std::vector<uint64_t> page(64, 0);
    uint32_t written = 0;
    uint64_t count = 0;
    if (ARNM_SUCCESS !=
        grdx_transactions_listing(index, &filter, skip, 64, false, page.data(), &written, &count)) {
      break;
    }
    all.insert(all.end(), page.begin(), page.begin() + written);
    if (written < 64) { break; }
    skip += written;
  }
  return all;
}

/** The numbers one side has and the other does not, as text for a failure message. */
std::string Difference(const std::vector<uint64_t> &got, const std::vector<uint64_t> &want) {
  std::string text;
  std::vector<uint64_t> only_got, only_want;
  std::set_difference(
      got.begin(), got.end(), want.begin(), want.end(), std::back_inserter(only_got)
  );
  std::set_difference(
      want.begin(), want.end(), got.begin(), got.end(), std::back_inserter(only_want)
  );
  for (uint64_t tx : only_got) { text += " +" + std::to_string(tx); }
  for (uint64_t tx : only_want) { text += " -" + std::to_string(tx); }
  return text;
}

/** Every number of a set, ascending. */
std::vector<uint32_t> SetValues(const arnm_roaring_bitmap &set) {
  std::vector<uint32_t> values(set.cardinality);
  if (!values.empty()) {
    EXPECT_EQ(arnm_roaring_page(&set, 0, set.cardinality, false, values.data()), set.cardinality);
  }
  return values;
}

/**
 * The three sets of an address say three different things, and the third one says what the
 * other two do not: a transaction an address signed, or whose balance it carries, is not one
 * that merely named it. Nothing but this checks the third set on its own -- a filter reads it
 * only inside the union.
 */
void ExpectRolesAreDistinct(
    const grdx_transactions *index, const Key &key, const std::vector<Record> &records
) {
  const grdx_address_sets *sets = grdx_transactions_address(index, key.data());
  ASSERT_NE(sets, nullptr);
  const std::vector<uint32_t> balance = SetValues(sets->balance);
  const std::vector<uint32_t> signed_ = SetValues(sets->signed_);
  const std::vector<uint32_t> other = SetValues(sets->other);
  for (uint32_t tx : other) {
    EXPECT_FALSE(std::binary_search(balance.begin(), balance.end(), tx))
        << "tx " << tx << " is in other and in balance";
    EXPECT_FALSE(std::binary_search(signed_.begin(), signed_.end(), tx))
        << "tx " << tx << " is in other and in signed";
  }
  // and each set holds what the reference put in it
  std::vector<uint32_t> want_balance, want_signed, want_other;
  for (const Record &record : records) {
    const uint32_t offset = static_cast<uint32_t>(record.tx_nr - index->base_tx_nr);
    if (record.balance.count(key)) { want_balance.push_back(offset); }
    if (record.signer.count(key)) { want_signed.push_back(offset); }
    if (record.other.count(key)) { want_other.push_back(offset); }
  }
  EXPECT_EQ(balance, want_balance);
  EXPECT_EQ(signed_, want_signed);
  EXPECT_EQ(other, want_other);
}

/** Every page of every shape, against the reference. */
void ExpectSameAnswers(
    const grdx_transactions *index,
    const std::vector<uint64_t> &expected,
    const grdx_transactions_filter &filter,
    const std::string &what
) {
  uint64_t count = 0;
  ASSERT_EQ(grdx_transactions_count(index, &filter, &count), ARNM_SUCCESS) << what;
  EXPECT_EQ(count, expected.size()) << what << " the index has, the reference has not:"
                                    << Difference(AllMatches(index, filter), expected);

  for (bool descending : {false, true}) {
    for (uint32_t skip : {0u, 1u, 7u}) {
      const uint32_t size = 5;
      std::vector<uint64_t> page(size, 0);
      uint32_t written = 0;
      uint64_t listed = 0;
      ASSERT_EQ(
          grdx_transactions_listing(
              index, &filter, skip, size, descending, page.data(), &written, &listed
          ),
          ARNM_SUCCESS
      ) << what;
      page.resize(written);
      EXPECT_EQ(listed, expected.size()) << what;
      std::vector<uint64_t> want;
      for (size_t i = skip; i < expected.size() && want.size() < size; ++i) {
        want.push_back(descending ? expected[expected.size() - 1u - i] : expected[i]);
      }
      EXPECT_EQ(page, want) << what << " skip " << skip << " descending " << descending;
    }
  }

  uint64_t newest = 0;
  const bool found = grdx_transactions_newest(index, &filter, &newest);
  EXPECT_EQ(found, !expected.empty()) << what;
  if (found) { EXPECT_EQ(newest, expected.back()) << what; }
}

/**
 * A chain of transactions that share addresses, types, coins and days, and the reference beside
 * it. Address 0 signs everything, so one set is dense; most addresses appear a few times, which
 * is what an ordinary address looks like.
 */
void BuildChain(
    Index &index,
    std::vector<Record> &records,
    const Uuid &chain_uuid,
    uint32_t count,
    uint32_t address_count,
    std::mt19937 &random,
    uint64_t first_tx_nr
) {
  const int64_t start = 1700000000;
  int64_t seconds = start;
  for (uint32_t i = 0; i < count; ++i) {
    Transaction transaction;
    grdr_complete_transaction *tx = &transaction.tx;
    grdr_complete_transaction_init(tx);
    tx->tx_nr = first_tx_nr + i;
    // several transactions a day, and now and then a day with none at all
    if (random() % 5 == 0) { seconds += 86400 * (1 + static_cast<int64_t>(random() % 3)); }
    seconds += static_cast<int64_t>(random() % 600);
    tx->confirmed_at.seconds = seconds;
    memcpy(tx->tx_community_uuid, chain_uuid.data(), ARNM_UUID_BINARY_SIZE);

    Record record;
    record.tx_nr = tx->tx_nr;
    record.seconds = seconds;

    static const grdt_transaction types[4] = {
        GRDT_TRANSACTION_TRANSFER, GRDT_TRANSACTION_CREATION, GRDT_TRANSACTION_REGISTER_ADDRESS,
        GRDT_TRANSACTION_DEFERRED_TRANSFER
    };
    tx->transaction_type = types[random() % 4];
    record.type = tx->transaction_type;

    const Key sender = MakeKey(random() % address_count);
    const Key recipient = MakeKey(random() % address_count);
    if (GRDT_TRANSACTION_REGISTER_ADDRESS == tx->transaction_type) {
      memcpy(tx->register_address.user_public_key, sender.data(), SIGN_PUBLIC_KEY_SIZE);
      memcpy(tx->register_address.account_public_key, recipient.data(), SIGN_PUBLIC_KEY_SIZE);
    } else {
      if (GRDT_TRANSACTION_CREATION != tx->transaction_type) {
        memcpy(tx->transfer.sender_pubkey, sender.data(), SIGN_PUBLIC_KEY_SIZE);
      }
      memcpy(tx->transfer.recipient_pubkey, recipient.data(), SIGN_PUBLIC_KEY_SIZE);
    }

    // one address signs every transaction, plus sometimes the sender
    const Key signer = MakeKey(0);
    transaction.signatures.push_back({});
    memcpy(transaction.signatures.back().public_key, signer.data(), SIGN_PUBLIC_KEY_SIZE);
    record.signer.insert(signer);
    if (random() % 3 == 0) {
      transaction.signatures.push_back({});
      memcpy(transaction.signatures.back().public_key, recipient.data(), SIGN_PUBLIC_KEY_SIZE);
      record.signer.insert(recipient);
    }

    // balances: the recipient most of the time, in the chain's coin or a foreign one
    if (random() % 4) {
      grdw_account_balance balance{};
      memcpy(balance.pubkey, recipient.data(), SIGN_PUBLIC_KEY_SIZE);
      const bool foreign = random() % 6 == 0;
      const Uuid coin = foreign ? MakeUuid(static_cast<uint8_t>(random() % 3)) : chain_uuid;
      memcpy(balance.community_uuid, coin.data(), ARNM_UUID_BINARY_SIZE);
      transaction.balances.push_back(balance);
      record.balance.insert(recipient);
      if (foreign) { record.foreign.insert(coin); }
    }

    tx->signature_pairs = transaction.signatures.data();
    tx->signature_pairs_count = transaction.signatures.size();
    tx->account_balances = transaction.balances.data();
    tx->account_balances_count = transaction.balances.size();

    // whoever the body names without signing and without a balance entry. By position, not by
    // value: a creation has no sender, and sender and recipient may be the same address
    const Key body[2] = {sender, recipient};
    for (int slot = 0; slot < 2; ++slot) {
      if (GRDT_TRANSACTION_CREATION == tx->transaction_type && 0 == slot) { continue; }
      if (!record.signer.count(body[slot]) && !record.balance.count(body[slot])) {
        record.other.insert(body[slot]);
      }
    }

    ASSERT_EQ(grdx_transactions_add(&index.index, tx), ARNM_SUCCESS) << "tx " << i;
    records.push_back(record);
  }
}

/** The chain file the benchmarks use, when it is there. */
std::string ChainPath() {
  if (const char *path = std::getenv("GRD_BENCH_CHAIN_DATA")) { return path; }
  return "../gradido_blockchain/build/tests/data/blk00000001.dat";
}

/** What the index reads from a transaction, read a second time by hand for the reference. */
Record RecordOf(const grdr_complete_transaction *tx, const Uuid &chain_uuid) {
  Record record;
  record.tx_nr = tx->tx_nr;
  record.seconds = tx->confirmed_at.seconds;
  record.type = tx->transaction_type;
  const Key zero{};
  for (size_t i = 0; i < tx->signature_pairs_count; ++i) {
    Key key{};
    memcpy(key.data(), tx->signature_pairs[i].public_key, SIGN_PUBLIC_KEY_SIZE);
    if (key != zero) { record.signer.insert(key); }
  }
  for (size_t i = 0; i < tx->account_balances_count; ++i) {
    Key key{};
    memcpy(key.data(), tx->account_balances[i].pubkey, SIGN_PUBLIC_KEY_SIZE);
    if (key != zero) { record.balance.insert(key); }
    Uuid coin{};
    memcpy(coin.data(), tx->account_balances[i].community_uuid, ARNM_UUID_BINARY_SIZE);
    if (coin != chain_uuid) { record.foreign.insert(coin); }
  }
  const uint8_t *named[3] = {nullptr, nullptr, nullptr};
  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_CREATION:
  case GRDT_TRANSACTION_TRANSFER:
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    named[0] = tx->transfer.sender_pubkey;
    named[1] = tx->transfer.recipient_pubkey;
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    named[0] = tx->register_address.user_public_key;
    named[1] = tx->register_address.account_public_key;
    break;
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    named[0] = tx->community_root.public_key;
    named[1] = tx->community_root.gmw_public_key;
    named[2] = tx->community_root.auf_public_key;
    break;
  default:
    break;
  }
  for (const uint8_t *key_bytes : named) {
    if (!key_bytes) { continue; }
    Key key{};
    memcpy(key.data(), key_bytes, SIGN_PUBLIC_KEY_SIZE);
    if (key == zero || record.signer.count(key) || record.balance.count(key)) { continue; }
    record.other.insert(key);
  }
  return record;
}

/**
 * A real chain: every transaction of the file into the index and into the reference, then the
 * filters a node asks -- an address's transactions, the balance changing ones, one type, one
 * day. What a synthetic chain cannot bring is the distribution: one address signs thousands of
 * transactions, most appear twice.
 */
TEST(TransactionsIndex, ARealChainMatchesTheReference) {
  const std::string path = ChainPath();
  FILE *file = fopen(path.c_str(), "rb");
  if (!file) { GTEST_SKIP() << "no chain file at " << path; }

  Uuid chain_uuid{};
  memcpy(chain_uuid.data(), bench_default_community_uuid, ARNM_UUID_BINARY_SIZE);
  fclose(file);
  Index index;
  std::vector<Record> records;
  std::map<Key, uint32_t> seen;

  struct Read {
    grdx_transactions *index;
    std::vector<Record> *records;
    std::map<Key, uint32_t> *seen;
    Uuid chain_uuid;
  } read{&index.index, &records, &seen, chain_uuid};
  bench_chain_source file_source;
  file_source.path = path.c_str();
  file_source.count = 0;
  file_source.seed = 0;
  ASSERT_EQ(
      bench_chain_source_feed(
          &file_source, bench_default_community_uuid,
          [](const grdr_complete_transaction *tx, void *context) -> arnm_result {
            Read &read = *static_cast<Read *>(context);
            const arnm_result result = grdx_transactions_add(read.index, tx);
            if (ARNM_SUCCESS != result) { return result; }
            read.records->push_back(RecordOf(tx, read.chain_uuid));
            for (const Key &key : read.records->back().signer) { ++(*read.seen)[key]; }
            for (const Key &key : read.records->back().balance) { ++(*read.seen)[key]; }
            for (const Key &key : read.records->back().other) { ++(*read.seen)[key]; }
            return ARNM_SUCCESS;
          },
          &read, nullptr
      ),
      ARNM_SUCCESS
  );

  ASSERT_GT(records.size(), 100u) << "the file holds no transactions this build can read";
  EXPECT_EQ(grdx_transactions_size(&index.index), records.size());
  EXPECT_EQ(grdx_transactions_address_count(&index.index), seen.size());

  // the busiest address and a handful of ordinary ones
  std::vector<std::pair<uint32_t, Key>> by_activity;
  for (const auto &entry : seen) { by_activity.push_back({entry.second, entry.first}); }
  std::sort(by_activity.begin(), by_activity.end(), [](const auto &a, const auto &b) {
    return a.first > b.first;
  });
  std::vector<Key> sample = {by_activity.front().second};
  for (size_t i = 1; i < by_activity.size(); i += by_activity.size() / 5 + 1) {
    sample.push_back(by_activity[i].second);
  }

  for (const Key &key : sample) {
    ExpectRolesAreDistinct(&index.index, key, records);
    for (grdx_address_role role : {GRDX_ADDRESS_ROLE_INVOLVED, GRDX_ADDRESS_ROLE_BALANCE}) {
      for (grdt_transaction type : {GRDT_TRANSACTION_NONE, GRDT_TRANSACTION_TRANSFER}) {
        grdx_transactions_filter filter{};
        ASSERT_EQ(grdx_transactions_filter_set_address(&filter, key.data(), role), ARNM_SUCCESS);
        ASSERT_EQ(grdx_transactions_filter_set_transaction_type(&filter, type), ARNM_SUCCESS);
        ExpectSameAnswers(
            &index.index, Matching(records, chain_uuid, &key, role, type, nullptr, 0, 0, 0, 0),
            filter, "real chain role " + std::to_string(role) + " type " + std::to_string(type)
        );
      }
    }
  }

  // a type over the whole chain, and one day out of it
  for (grdt_transaction type :
       {GRDT_TRANSACTION_TRANSFER, GRDT_TRANSACTION_CREATION, GRDT_TRANSACTION_REGISTER_ADDRESS}) {
    grdx_transactions_filter filter{};
    ASSERT_EQ(grdx_transactions_filter_set_transaction_type(&filter, type), ARNM_SUCCESS);
    ExpectSameAnswers(
        &index.index,
        Matching(records, chain_uuid, nullptr, GRDX_ADDRESS_ROLE_NONE, type, nullptr, 0, 0, 0, 0),
        filter, "real chain type " + std::to_string(type)
    );
  }
  const int64_t middle_day = records[records.size() / 2].seconds / 86400;
  grdx_transactions_filter day_filter{};
  day_filter.from_seconds = middle_day * 86400;
  day_filter.to_seconds = middle_day * 86400 + 86399;
  ExpectSameAnswers(
      &index.index,
      Matching(
          records, chain_uuid, nullptr, GRDX_ADDRESS_ROLE_NONE, GRDT_TRANSACTION_NONE, nullptr, 0,
          0, day_filter.from_seconds, day_filter.to_seconds
      ),
      day_filter, "real chain one day"
  );
}

/**
 * The generated chain: as long as a node's, shaped like the one in the tree, and there whether
 * or not that file is. The reference here is not every transaction -- a hundred thousand of
 * those would cost more than the index does -- but what can be counted while reading: how many
 * of each type, and every transaction a sample of addresses took part in.
 */
TEST(TransactionsIndex, AGeneratedChainMatchesTheReference) {
  const bench_chain_source source = bench_chain_source_of(0, nullptr);
  Uuid chain_uuid{};
  memcpy(chain_uuid.data(), bench_default_community_uuid, ARNM_UUID_BINARY_SIZE);

  Index index;

  // two passes: the first fills the index and picks the sample, the second follows the sample
  // one address out of every so many transactions, whatever length the chain has
  const uint32_t sample_step = 1u + (source.path ? 997u : source.count / 32u);
  struct Pass1 {
    grdx_transactions *index;
    uint32_t sample_step;
    std::array<uint32_t, GRDT_TRANSACTION_COUNT> by_type{};
    std::vector<Key> sample;
    uint32_t seen = 0;
    uint64_t first_tx = 0;
    uint64_t last_tx = 0;
  } pass1;
  pass1.index = &index.index;
  pass1.sample_step = sample_step;
  ASSERT_EQ(
      bench_chain_source_feed(
          &source, bench_default_community_uuid,
          [](const grdr_complete_transaction *tx, void *context) -> arnm_result {
            Pass1 &pass = *static_cast<Pass1 *>(context);
            const arnm_result result = grdx_transactions_add(pass.index, tx);
            if (ARNM_SUCCESS != result) { return result; }
            if (!pass.seen) { pass.first_tx = tx->tx_nr; }
            pass.last_tx = tx->tx_nr;
            ++pass.seen;
            pass.by_type[static_cast<size_t>(tx->transaction_type)]++;
            // every so often an address, so the sample holds busy ones and quiet ones
            if (pass.sample.size() < 24 && pass.seen % pass.sample_step == 0 &&
                tx->account_balances_count) {
              Key key{};
              memcpy(key.data(), tx->account_balances[0].pubkey, SIGN_PUBLIC_KEY_SIZE);
              pass.sample.push_back(key);
            }
            return ARNM_SUCCESS;
          },
          &pass1, nullptr
      ),
      ARNM_SUCCESS
  );
  ASSERT_GT(pass1.seen, 0u);
  // a generated chain is exactly as long as it was asked to be; a file is as long as it is
  if (!source.path) { EXPECT_EQ(pass1.seen, source.count); }
  EXPECT_EQ(grdx_transactions_size(&index.index), pass1.seen);

  // the same chain again, this time following what the sampled addresses did
  struct Pass2 {
    const std::vector<Key> *sample;
    std::vector<std::vector<uint64_t>> involved;
    std::vector<std::vector<uint64_t>> balance;
    Uuid chain_uuid{};
  } pass2;
  pass2.sample = &pass1.sample;
  pass2.involved.resize(pass1.sample.size());
  pass2.balance.resize(pass1.sample.size());
  pass2.chain_uuid = chain_uuid;
  ASSERT_EQ(
      bench_chain_source_feed(
          &source, bench_default_community_uuid,
          [](const grdr_complete_transaction *tx, void *context) -> arnm_result {
            Pass2 &pass = *static_cast<Pass2 *>(context);
            const Record record = RecordOf(tx, pass.chain_uuid);
            for (size_t i = 0; i < pass.sample->size(); ++i) {
              const Key &key = (*pass.sample)[i];
              if (record.balance.count(key)) { pass.balance[i].push_back(tx->tx_nr); }
              if (record.balance.count(key) || record.signer.count(key) ||
                  record.other.count(key)) {
                pass.involved[i].push_back(tx->tx_nr);
              }
            }
            return ARNM_SUCCESS;
          },
          &pass2, nullptr
      ),
      ARNM_SUCCESS
  );

  // every type, counted from the sets against what was read
  for (uint32_t type = 1; type < GRDT_TRANSACTION_COUNT; ++type) {
    grdx_transactions_filter filter{};
    ASSERT_EQ(
        grdx_transactions_filter_set_transaction_type(&filter, static_cast<grdt_transaction>(type)),
        ARNM_SUCCESS
    );
    uint64_t count = 0;
    ASSERT_EQ(grdx_transactions_count(&index.index, &filter, &count), ARNM_SUCCESS);
    EXPECT_EQ(count, pass1.by_type[type]) << "type " << type;
  }

  // a filter that names nothing is the whole chain
  grdx_transactions_filter everything{};
  uint64_t all = 0;
  ASSERT_EQ(grdx_transactions_count(&index.index, &everything, &all), ARNM_SUCCESS);
  EXPECT_EQ(all, pass1.seen);
  uint64_t newest = 0;
  ASSERT_TRUE(grdx_transactions_newest(&index.index, &everything, &newest));
  EXPECT_EQ(newest, pass1.last_tx);

  // and the sampled addresses, in both roles
  for (size_t i = 0; i < pass1.sample.size(); ++i) {
    const Key &key = pass1.sample[i];
    grdx_transactions_filter filter{};
    ASSERT_EQ(
        grdx_transactions_filter_set_address(&filter, key.data(), GRDX_ADDRESS_ROLE_INVOLVED),
        ARNM_SUCCESS
    );
    ExpectSameAnswers(&index.index, pass2.involved[i], filter, "generated involved");
    ASSERT_EQ(
        grdx_transactions_filter_set_address(&filter, key.data(), GRDX_ADDRESS_ROLE_BALANCE),
        ARNM_SUCCESS
    );
    ExpectSameAnswers(&index.index, pass2.balance[i], filter, "generated balance");
  }
}

TEST(TransactionsIndex, AnEmptyIndexAnswersNothing) {
  Index index;
  grdx_transactions_filter filter{};
  uint64_t count = 7;
  ASSERT_EQ(grdx_transactions_count(&index.index, &filter, &count), ARNM_SUCCESS);
  EXPECT_EQ(count, 0u);
  uint64_t newest = 7;
  EXPECT_FALSE(grdx_transactions_newest(&index.index, &filter, &newest));
  EXPECT_EQ(newest, 7u);
  EXPECT_EQ(grdx_transactions_address_count(&index.index), 0u);
  EXPECT_EQ(grdx_transactions_size(&index.index), 0u);
  EXPECT_EQ(grdx_transactions_address(&index.index, MakeKey(1).data()), nullptr);
}

TEST(TransactionsIndex, EveryFilterMatchesTheReference) {
  const Uuid chain_uuid = MakeUuid(200);
  Index index;
  std::vector<Record> records;
  std::mt19937 random(11);
  BuildChain(index, records, chain_uuid, 400, 25, random, 1);
  ASSERT_EQ(grdx_transactions_size(&index.index), 400u);

  for (uint32_t address = 0; address < 6; ++address) {
    ExpectRolesAreDistinct(&index.index, MakeKey(address), records);
  }

  const std::vector<grdt_transaction> types = {
      GRDT_TRANSACTION_NONE, GRDT_TRANSACTION_TRANSFER, GRDT_TRANSACTION_CREATION,
      GRDT_TRANSACTION_COMMUNITY_ROOT
  };
  const Uuid foreign = MakeUuid(1);
  for (uint32_t address = 0; address < 6; ++address) {
    const Key key = MakeKey(address);
    for (grdx_address_role role :
         {GRDX_ADDRESS_ROLE_NONE, GRDX_ADDRESS_ROLE_INVOLVED, GRDX_ADDRESS_ROLE_BALANCE}) {
      for (grdt_transaction type : types) {
        for (int coin_case = 0; coin_case < 3; ++coin_case) {
          const Uuid *coin = coin_case == 0 ? nullptr : coin_case == 1 ? &chain_uuid : &foreign;
          grdx_transactions_filter filter{};
          ASSERT_EQ(grdx_transactions_filter_set_address(&filter, key.data(), role), ARNM_SUCCESS);
          ASSERT_EQ(grdx_transactions_filter_set_transaction_type(&filter, type), ARNM_SUCCESS);
          ASSERT_EQ(
              grdx_transactions_filter_set_coin_community(&filter, coin ? coin->data() : nullptr),
              ARNM_SUCCESS
          );
          const std::vector<uint64_t> expected =
              Matching(records, chain_uuid, &key, role, type, coin, 0, 0, 0, 0);
          ExpectSameAnswers(
              &index.index, expected, filter,
              "address " + std::to_string(address) + " role " + std::to_string(role) + " type " +
                  std::to_string(type) + " coin " + std::to_string(coin_case)
          );
        }
      }
    }
  }
}

TEST(TransactionsIndex, RangesOfNumbersAndOfDaysNarrowTheSameWay) {
  const Uuid chain_uuid = MakeUuid(7);
  // an index that does not start at zero: the sets hold offsets, every answer holds numbers
  grdx_transactions_options options{};
  options.base_tx_nr = 5000;
  Index index(&options);
  std::vector<Record> records;
  std::mt19937 random(23);
  BuildChain(index, records, chain_uuid, 300, 12, random, 5000);

  const int64_t first = records.front().seconds;
  const int64_t last = records.back().seconds;
  for (int probe = 0; probe < 12; ++probe) {
    grdx_transactions_filter filter{};
    const Key key = MakeKey(probe % 4);
    if (probe % 2) {
      ASSERT_EQ(
          grdx_transactions_filter_set_address(&filter, key.data(), GRDX_ADDRESS_ROLE_INVOLVED),
          ARNM_SUCCESS
      );
    }
    uint64_t min_tx = 0, max_tx = 0;
    int64_t from = 0, to = 0;
    if (probe % 3 == 0) {
      min_tx = records[static_cast<size_t>(random() % records.size())].tx_nr;
      max_tx = records[static_cast<size_t>(random() % records.size())].tx_nr;
      if (min_tx > max_tx) { std::swap(min_tx, max_tx); }
    }
    if (probe % 3 != 1) {
      from = first + static_cast<int64_t>(random() % static_cast<uint64_t>(last - first + 1));
      to = first + static_cast<int64_t>(random() % static_cast<uint64_t>(last - first + 1));
      if (from > to) { std::swap(from, to); }
    }
    filter.min_tx_nr = min_tx;
    filter.max_tx_nr = max_tx;
    filter.from_seconds = from;
    filter.to_seconds = to;
    const std::vector<uint64_t> expected = Matching(
        records, chain_uuid, grdx_transactions_filter_address(&filter) ? &key : nullptr,
        filter.role, GRDT_TRANSACTION_NONE, nullptr, min_tx, max_tx, from, to
    );
    ExpectSameAnswers(&index.index, expected, filter, "probe " + std::to_string(probe));
  }
}

TEST(TransactionsIndex, ADayTableTurnsSecondsIntoNumbers) {
  const Uuid chain_uuid = MakeUuid(3);
  Index index;
  std::vector<Record> records;
  std::mt19937 random(31);
  BuildChain(index, records, chain_uuid, 200, 8, random, 1);

  uint64_t min = 0, max = 0;
  ASSERT_TRUE(grdx_transactions_range_of_days(&index.index, 0, 0, &min, &max));
  EXPECT_EQ(min, records.front().tx_nr);
  EXPECT_EQ(max, records.back().tx_nr);

  // a span of one day holds exactly the transactions confirmed that day
  const int64_t day = records[records.size() / 2].seconds / 86400;
  ASSERT_TRUE(
      grdx_transactions_range_of_days(&index.index, day * 86400, day * 86400 + 86399, &min, &max)
  );
  for (const Record &record : records) {
    const bool inside = record.tx_nr >= min && record.tx_nr <= max;
    EXPECT_EQ(inside, record.seconds / 86400 == day) << "tx " << record.tx_nr;
  }

  // a span before the chain and one after it hold nothing
  EXPECT_FALSE(
      grdx_transactions_range_of_days(&index.index, 1, records.front().seconds - 86400, &min, &max)
  );
  EXPECT_FALSE(grdx_transactions_range_of_days(
      &index.index, records.back().seconds + 86400 * 2, records.back().seconds + 86400 * 4, &min,
      &max
  ));
}

/**
 * The first transaction carries its community's own coin, like every other one. Whatever the
 * index learns about the chain from it has to be known before its balances are read, or the
 * chain's own coin looks foreign to it -- once, in the one transaction that defines it.
 */
TEST(TransactionsIndex, TheFirstTransactionsCoinIsNotForeign) {
  const Uuid chain_uuid = MakeUuid(77);
  Index index;
  std::vector<Record> records;

  for (uint32_t i = 0; i < 3; ++i) {
    Transaction transaction;
    grdr_complete_transaction *tx = &transaction.tx;
    grdr_complete_transaction_init(tx);
    tx->tx_nr = 1u + i;
    tx->confirmed_at.seconds = 1700000000 + i * 3600;
    tx->transaction_type = GRDT_TRANSACTION_TRANSFER;
    memcpy(tx->tx_community_uuid, chain_uuid.data(), ARNM_UUID_BINARY_SIZE);
    const Key sender = MakeKey(1);
    const Key recipient = MakeKey(2);
    memcpy(tx->transfer.sender_pubkey, sender.data(), SIGN_PUBLIC_KEY_SIZE);
    memcpy(tx->transfer.recipient_pubkey, recipient.data(), SIGN_PUBLIC_KEY_SIZE);
    grdw_account_balance balance{};
    memcpy(balance.pubkey, recipient.data(), SIGN_PUBLIC_KEY_SIZE);
    memcpy(balance.community_uuid, chain_uuid.data(), ARNM_UUID_BINARY_SIZE);
    transaction.balances = {balance};
    tx->account_balances = transaction.balances.data();
    tx->account_balances_count = transaction.balances.size();
    ASSERT_EQ(grdx_transactions_add(&index.index, tx), ARNM_SUCCESS) << "tx " << tx->tx_nr;

    Record record;
    record.tx_nr = tx->tx_nr;
    record.seconds = tx->confirmed_at.seconds;
    record.type = tx->transaction_type;
    record.balance.insert(recipient);
    record.other.insert(sender);
    records.push_back(record);
  }

  EXPECT_EQ(index.index.coin_community_count, 0u)
      << "no transaction here carries a coin that is not the chain's own";

  // "only this chain's coin" has to hold every one of them, the first included
  grdx_transactions_filter filter{};
  ASSERT_EQ(grdx_transactions_filter_set_coin_community(&filter, chain_uuid.data()), ARNM_SUCCESS);
  ExpectSameAnswers(
      &index.index,
      Matching(
          records, chain_uuid, nullptr, GRDX_ADDRESS_ROLE_NONE, GRDT_TRANSACTION_NONE, &chain_uuid,
          0, 0, 0, 0
      ),
      filter, "the chain's own coin"
  );
}

TEST(TransactionsIndex, WhatItRefuses) {
  grdx_transactions_options options{};
  options.base_tx_nr = 100;
  Index index(&options);

  Transaction transaction;
  grdr_complete_transaction *tx = &transaction.tx;
  grdr_complete_transaction_init(tx);
  tx->transaction_type = GRDT_TRANSACTION_TRANSFER;
  tx->confirmed_at.seconds = 1700000000;
  tx->tx_nr = 99;
  EXPECT_EQ(grdx_transactions_add(&index.index, tx), ARNM_ERROR_INVALID_PARAM);
  tx->tx_nr = 100;
  EXPECT_EQ(grdx_transactions_add(&index.index, tx), ARNM_SUCCESS);
  // the same number again, and a number that leaves a gap: a chain has neither
  EXPECT_EQ(grdx_transactions_add(&index.index, tx), ARNM_ERROR_INVALID_PARAM);
  tx->tx_nr = 102;
  EXPECT_EQ(grdx_transactions_add(&index.index, tx), ARNM_ERROR_INVALID_PARAM);
  tx->tx_nr = 101;
  tx->confirmed_at.seconds = 1700000000 - 86400 * 2;
  EXPECT_EQ(grdx_transactions_add(&index.index, tx), ARNM_ERROR_INVALID_PARAM);
  tx->confirmed_at.seconds = 1700000000;
  EXPECT_EQ(grdx_transactions_add(&index.index, tx), ARNM_SUCCESS);

  // past base + UINT32_MAX, which only a first transaction can reach
  grdx_transactions_options far_options{};
  far_options.base_tx_nr = 100;
  Index far(&far_options);
  tx->tx_nr = 100ull + UINT32_MAX + 1ull;
  EXPECT_EQ(grdx_transactions_add(&far.index, tx), ARNM_ERROR_RESOURCE_SIZE_EXCEED);

  EXPECT_EQ(grdx_transactions_add(nullptr, tx), ARNM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdx_transactions_add(&index.index, nullptr), ARNM_ERROR_NULL_POINTER);
  grdx_transactions_filter filter{};
  uint64_t count = 0;
  EXPECT_EQ(grdx_transactions_count(&index.index, &filter, nullptr), ARNM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdx_transactions_count(nullptr, &filter, &count), ARNM_ERROR_NULL_POINTER);
  uint64_t page[2] = {0, 0};
  uint32_t written = 0;
  EXPECT_EQ(
      grdx_transactions_listing(
          &index.index, &filter, 0, GRDX_PAGE_MAX + 1u, false, page, &written, &count
      ),
      ARNM_ERROR_INVALID_PARAM
  );

  // an index that was never initialised says so rather than reading its own zeroes
  grdx_transactions raw{};
  EXPECT_EQ(grdx_transactions_count(&raw, &filter, &count), ARNM_ERROR_INVALID_STATE);
  EXPECT_EQ(grdx_transactions_add(&raw, tx), ARNM_ERROR_INVALID_STATE);
}

TEST(TransactionsIndex, ResetKeepsTheMemoryAndForgetsTheChain) {
  const Uuid chain_uuid = MakeUuid(9);
  Index index;
  std::vector<Record> records;
  std::mt19937 random(41);
  BuildChain(index, records, chain_uuid, 120, 10, random, 1);
  const uint64_t lent = index.index.pool.lent_bytes;
  EXPECT_GT(lent, 0u);

  grdx_transactions_reset(&index.index);
  EXPECT_EQ(grdx_transactions_size(&index.index), 0u);
  EXPECT_EQ(grdx_transactions_address_count(&index.index), 0u);
  EXPECT_EQ(index.index.pool.lent_bytes, 0u);
  EXPECT_GT(index.index.pool.cached_bytes, 0u) << "the blocks stay with the pool";

  // the same chain again, and the same answers
  std::vector<Record> again;
  std::mt19937 same(41);
  BuildChain(index, again, chain_uuid, 120, 10, same, 1);
  grdx_transactions_filter filter{};
  const Key key = MakeKey(0);
  ASSERT_EQ(
      grdx_transactions_filter_set_address(&filter, key.data(), GRDX_ADDRESS_ROLE_INVOLVED),
      ARNM_SUCCESS
  );
  ExpectSameAnswers(
      &index.index,
      Matching(again, chain_uuid, &key, filter.role, GRDT_TRANSACTION_NONE, nullptr, 0, 0, 0, 0),
      filter, "after reset"
  );
}

} // namespace
