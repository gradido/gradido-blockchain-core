#include <gtest/gtest.h>

#include "gradido_blockchain_core/blockchain/addresses.h"

#include "bench_chain_data.h"
#include "bench_chain_synth.h"

#include "memory_limit.h"
#include "starved_arena.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

/*
 * grdb_addresses against a reference that keeps the same two facts in a std::map: the
 * transactions that gave an address a type, and the last one that moved its balance. The index
 * is filled from the same transactions, so a disagreement is the index's.
 *
 * Chain file: $GRD_BENCH_CHAIN_DATA, else ../gradido_blockchain/build/tests/data/blk00000001.dat
 * from the working directory. That case is skipped when neither exists.
 */

namespace {

using Key = std::array<uint8_t, SIGN_PUBLIC_KEY_SIZE>;

/** What the reference remembers of one address. */
struct Address {
  std::vector<std::pair<uint64_t, grdt_address>> types; /**< oldest first */
  uint64_t last_balance = 0;
};

using Reference = std::map<Key, Address>;

Key MakeKey(uint32_t n) {
  Key key{};
  for (size_t i = 0; i < key.size(); ++i) { key[i] = static_cast<uint8_t>(n * 37u + i * 5u + 1u); }
  return key;
}

/** A transaction built for the index, with the arrays it points at kept beside it. */
struct Transaction {
  grdr_complete_transaction tx{};
  std::vector<grdw_account_balance> balances;
};

class Index {
public:
  explicit Index(const grdb_addresses_options *options = nullptr) {
    EXPECT_EQ(grdb_addresses_init(&index, options, nullptr), ARNM_SUCCESS);
  }
  ~Index() {
    grdb_addresses_release(&index);
  }
  Index(const Index &) = delete;
  Index &operator=(const Index &) = delete;
  grdb_addresses index{};
};

/** The reference's answer to "what was this address as of @p at". */
bool TypeAt(const Address &address, uint64_t at, grdt_address *out) {
  for (auto it = address.types.rbegin(); it != address.types.rend(); ++it) {
    if (it->first <= at) {
      *out = it->second;
      return true;
    }
  }
  return false;
}

/** Every address of the reference, asked every way the index can be asked. */
void ExpectSameAnswers(const grdb_addresses *index, const Reference &reference, uint64_t max_tx) {
  for (const auto &entry : reference) {
    const Key &key = entry.first;
    const Address &address = entry.second;
    EXPECT_TRUE(grdb_addresses_knows(index, key.data()));

    const grdt_address type = grdb_addresses_type(index, key.data());
    EXPECT_EQ(type, address.types.empty() ? GRDT_ADDRESS_NONE : address.types.back().second);

    for (uint64_t at = 0; at <= max_tx + 1u; ++at) {
      grdt_address want = GRDT_ADDRESS_NONE;
      const bool wanted = TypeAt(address, at, &want);
      grdt_address got = GRDT_ADDRESS_NONE;
      const bool found = grdb_addresses_type_at(index, key.data(), at, &got);
      ASSERT_EQ(found, wanted) << "at " << at;
      if (found) { EXPECT_EQ(got, want) << "at " << at; }
    }

    std::vector<uint64_t> changes(address.types.size() + 2u, 0);
    const uint32_t written = grdb_addresses_type_changes(
        index, key.data(), changes.data(), static_cast<uint32_t>(changes.size())
    );
    changes.resize(written);
    std::vector<uint64_t> want_changes;
    for (auto it = address.types.rbegin(); it != address.types.rend(); ++it) {
      want_changes.push_back(it->first);
    }
    EXPECT_EQ(changes, want_changes);

    uint64_t balance_tx = 0;
    const bool has_balance = grdb_addresses_last_balance(index, key.data(), &balance_tx);
    EXPECT_EQ(has_balance, address.last_balance != 0);
    if (has_balance) { EXPECT_EQ(balance_tx, address.last_balance); }
  }
}

/** A chain that types addresses every way a chain can, and moves balances in between. */
void BuildChain(Index &index, Reference &reference, uint64_t *max_tx) {
  uint64_t tx_nr = 1;
  auto add = [&](Transaction &transaction) {
    ASSERT_EQ(grdb_addresses_add(&index.index, &transaction.tx), ARNM_SUCCESS)
        << "tx " << transaction.tx.tx_nr;
    *max_tx = transaction.tx.tx_nr;
  };

  // the community root types its two accounts and leaves its own key untyped
  {
    Transaction transaction;
    grdr_complete_transaction_init(&transaction.tx);
    transaction.tx.tx_nr = tx_nr++;
    transaction.tx.transaction_type = GRDT_TRANSACTION_COMMUNITY_ROOT;
    memcpy(transaction.tx.community_root.public_key, MakeKey(1).data(), SIGN_PUBLIC_KEY_SIZE);
    memcpy(transaction.tx.community_root.gmw_public_key, MakeKey(2).data(), SIGN_PUBLIC_KEY_SIZE);
    memcpy(transaction.tx.community_root.auf_public_key, MakeKey(3).data(), SIGN_PUBLIC_KEY_SIZE);
    reference[MakeKey(2)].types.push_back({transaction.tx.tx_nr, GRDT_ADDRESS_COMMUNITY_GMW});
    reference[MakeKey(3)].types.push_back({transaction.tx.tx_nr, GRDT_ADDRESS_COMMUNITY_AUF});
    add(transaction);
  }

  // a registration types both of its keys
  const grdt_address registered[3] = {
      GRDT_ADDRESS_COMMUNITY_HUMAN, GRDT_ADDRESS_SUBACCOUNT, GRDT_ADDRESS_CRYPTO_ACCOUNT
  };
  for (uint32_t i = 0; i < 3; ++i) {
    Transaction transaction;
    grdr_complete_transaction_init(&transaction.tx);
    transaction.tx.tx_nr = tx_nr++;
    transaction.tx.transaction_type = GRDT_TRANSACTION_REGISTER_ADDRESS;
    transaction.tx.address_type = registered[i];
    const Key user = MakeKey(10u + i);
    const Key account = MakeKey(20u + i);
    memcpy(transaction.tx.register_address.user_public_key, user.data(), SIGN_PUBLIC_KEY_SIZE);
    memcpy(
        transaction.tx.register_address.account_public_key, account.data(), SIGN_PUBLIC_KEY_SIZE
    );
    reference[user].types.push_back({transaction.tx.tx_nr, registered[i]});
    reference[account].types.push_back({transaction.tx.tx_nr, registered[i]});
    add(transaction);
  }

  // a transfer with balances: no type given, two balances moved
  for (uint32_t i = 0; i < 4; ++i) {
    Transaction transaction;
    grdr_complete_transaction_init(&transaction.tx);
    transaction.tx.tx_nr = tx_nr++;
    transaction.tx.transaction_type = GRDT_TRANSACTION_TRANSFER;
    const Key sender = MakeKey(10u + i % 3u);
    const Key recipient = MakeKey(20u + i % 3u);
    memcpy(transaction.tx.transfer.sender_pubkey, sender.data(), SIGN_PUBLIC_KEY_SIZE);
    memcpy(transaction.tx.transfer.recipient_pubkey, recipient.data(), SIGN_PUBLIC_KEY_SIZE);
    grdw_account_balance from{};
    memcpy(from.pubkey, sender.data(), SIGN_PUBLIC_KEY_SIZE);
    grdw_account_balance to{};
    memcpy(to.pubkey, recipient.data(), SIGN_PUBLIC_KEY_SIZE);
    transaction.balances = {from, to};
    transaction.tx.account_balances = transaction.balances.data();
    transaction.tx.account_balances_count = transaction.balances.size();
    reference[sender].last_balance = transaction.tx.tx_nr;
    reference[recipient].last_balance = transaction.tx.tx_nr;
    add(transaction);
  }

  // a deferred transfer types its recipient, who was never registered
  {
    Transaction transaction;
    grdr_complete_transaction_init(&transaction.tx);
    transaction.tx.tx_nr = tx_nr++;
    transaction.tx.transaction_type = GRDT_TRANSACTION_DEFERRED_TRANSFER;
    const Key sender = MakeKey(10);
    const Key recipient = MakeKey(50);
    memcpy(transaction.tx.transfer.sender_pubkey, sender.data(), SIGN_PUBLIC_KEY_SIZE);
    memcpy(transaction.tx.transfer.recipient_pubkey, recipient.data(), SIGN_PUBLIC_KEY_SIZE);
    reference[recipient].types.push_back({transaction.tx.tx_nr, GRDT_ADDRESS_DEFERRED_TRANSFER});
    add(transaction);
  }

  // and one address registered a second time, with another type
  {
    Transaction transaction;
    grdr_complete_transaction_init(&transaction.tx);
    transaction.tx.tx_nr = tx_nr++;
    transaction.tx.transaction_type = GRDT_TRANSACTION_REGISTER_ADDRESS;
    transaction.tx.address_type = GRDT_ADDRESS_COMMUNITY_PROJECT;
    const Key user = MakeKey(10);
    memcpy(transaction.tx.register_address.user_public_key, user.data(), SIGN_PUBLIC_KEY_SIZE);
    reference[user].types.push_back({transaction.tx.tx_nr, GRDT_ADDRESS_COMMUNITY_PROJECT});
    add(transaction);
  }
}

TEST(AddressIndex, AnEmptyIndexKnowsNothing) {
  Index index;
  const Key key = MakeKey(1);
  EXPECT_EQ(grdb_addresses_size(&index.index), 0u);
  EXPECT_FALSE(grdb_addresses_knows(&index.index, key.data()));
  EXPECT_EQ(grdb_addresses_type(&index.index, key.data()), GRDT_ADDRESS_NONE);
  grdt_address type = GRDT_ADDRESS_SUBACCOUNT;
  EXPECT_FALSE(grdb_addresses_type_at(&index.index, key.data(), 99, &type));
  EXPECT_EQ(type, GRDT_ADDRESS_SUBACCOUNT);
  uint64_t tx_nr = 7;
  EXPECT_FALSE(grdb_addresses_last_balance(&index.index, key.data(), &tx_nr));
  EXPECT_EQ(tx_nr, 7u);
  EXPECT_EQ(grdb_addresses_type_changes(&index.index, key.data(), &tx_nr, 1), 0u);
}

TEST(AddressIndex, EveryWayAnAddressIsTypedMatchesTheReference) {
  Index index;
  Reference reference;
  uint64_t max_tx = 0;
  BuildChain(index, reference, &max_tx);

  EXPECT_EQ(grdb_addresses_size(&index.index), reference.size());
  ExpectSameAnswers(&index.index, reference, max_tx);

  // the community root's own key is named but never typed
  EXPECT_EQ(grdb_addresses_type(&index.index, MakeKey(1).data()), GRDT_ADDRESS_NONE);
  EXPECT_FALSE(grdb_addresses_knows(&index.index, MakeKey(1).data()));
  // an address registered twice answers with the type that stood at the time
  grdt_address type = GRDT_ADDRESS_NONE;
  ASSERT_TRUE(grdb_addresses_type_at(&index.index, MakeKey(10).data(), 2, &type));
  EXPECT_EQ(type, GRDT_ADDRESS_COMMUNITY_HUMAN);
  ASSERT_TRUE(grdb_addresses_type_at(&index.index, MakeKey(10).data(), max_tx, &type));
  EXPECT_EQ(type, GRDT_ADDRESS_COMMUNITY_PROJECT);
  EXPECT_EQ(grdb_addresses_type(&index.index, MakeKey(10).data()), GRDT_ADDRESS_COMMUNITY_PROJECT);
}

TEST(AddressIndex, WhatItRefuses) {
  Index index;
  Transaction transaction;
  grdr_complete_transaction_init(&transaction.tx);
  transaction.tx.transaction_type = GRDT_TRANSACTION_TRANSFER;
  transaction.tx.tx_nr = 5;
  EXPECT_EQ(grdb_addresses_add(&index.index, &transaction.tx), ARNM_SUCCESS);
  EXPECT_EQ(grdb_addresses_add(&index.index, &transaction.tx), ARNM_ERROR_INVALID_PARAM);
  transaction.tx.tx_nr = 4;
  EXPECT_EQ(grdb_addresses_add(&index.index, &transaction.tx), ARNM_ERROR_INVALID_PARAM);
  transaction.tx.tx_nr = 6;
  transaction.tx.transaction_type = static_cast<grdt_transaction>(GRDT_TRANSACTION_COUNT);
  EXPECT_EQ(grdb_addresses_add(&index.index, &transaction.tx), ARNM_ERROR_INVALID_ENUM_TYPE);
  transaction.tx.transaction_type = GRDT_TRANSACTION_REGISTER_ADDRESS;
  transaction.tx.address_type = static_cast<grdt_address>(GRDT_ADDRESS_DEFERRED_TRANSFER + 1);
  memcpy(transaction.tx.register_address.user_public_key, MakeKey(1).data(), SIGN_PUBLIC_KEY_SIZE);
  EXPECT_EQ(grdb_addresses_add(&index.index, &transaction.tx), ARNM_ERROR_INVALID_ENUM_TYPE);

  EXPECT_EQ(grdb_addresses_add(nullptr, &transaction.tx), ARNM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdb_addresses_add(&index.index, nullptr), ARNM_ERROR_NULL_POINTER);
  grdb_addresses raw{};
  EXPECT_EQ(grdb_addresses_add(&raw, &transaction.tx), ARNM_ERROR_INVALID_STATE);
  EXPECT_EQ(grdb_addresses_type(&raw, MakeKey(1).data()), GRDT_ADDRESS_NONE);
  EXPECT_EQ(grdb_addresses_size(&raw), 0u);
}

TEST(AddressIndex, ResetForgetsEveryAddress) {
  Index index;
  Reference reference;
  uint64_t max_tx = 0;
  BuildChain(index, reference, &max_tx);
  ASSERT_GT(grdb_addresses_size(&index.index), 0u);

  grdb_addresses_reset(&index.index);
  EXPECT_EQ(grdb_addresses_size(&index.index), 0u);
  EXPECT_FALSE(grdb_addresses_knows(&index.index, MakeKey(10).data()));
  EXPECT_EQ(grdb_addresses_type(&index.index, MakeKey(10).data()), GRDT_ADDRESS_NONE);

  // and the same chain again gives the same answers
  Reference again;
  uint64_t again_max = 0;
  BuildChain(index, again, &again_max);
  ExpectSameAnswers(&index.index, again, again_max);
}

/**
 * The generated chain, which is there whether or not the file in the tree is: every address it
 * types and every balance it moves, held against the same reference the real chain is.
 */
TEST(AddressIndex, AGeneratedChainMatchesTheReference) {
  const bench_chain_source source = bench_chain_source_of(0, nullptr);
  Index index;

  struct Fill {
    grdb_addresses *index;
    Reference *reference;
    uint64_t max_tx = 0;
    uint32_t seen = 0;
  } fill;
  Reference reference;
  fill.index = &index.index;
  fill.reference = &reference;

  ASSERT_EQ(
      bench_chain_source_feed(
          &source, bench_default_community_uuid,
          [](const grdr_complete_transaction *tx, void *context) -> arnm_result {
            Fill &fill = *static_cast<Fill *>(context);
            const arnm_result result = grdb_addresses_add(fill.index, tx);
            if (ARNM_SUCCESS != result) { return result; }
            fill.max_tx = tx->tx_nr;
            ++fill.seen;
            const Key zero{};
            auto note_type = [&](const uint8_t *key_bytes, grdt_address type) {
              if (!key_bytes) { return; }
              Key key{};
              memcpy(key.data(), key_bytes, SIGN_PUBLIC_KEY_SIZE);
              if (key == zero) { return; }
              (*fill.reference)[key].types.push_back({tx->tx_nr, type});
            };
            switch (tx->transaction_type) {
            case GRDT_TRANSACTION_COMMUNITY_ROOT:
              note_type(tx->community_root.gmw_public_key, GRDT_ADDRESS_COMMUNITY_GMW);
              note_type(tx->community_root.auf_public_key, GRDT_ADDRESS_COMMUNITY_AUF);
              break;
            case GRDT_TRANSACTION_REGISTER_ADDRESS:
              note_type(tx->register_address.user_public_key, tx->address_type);
              note_type(tx->register_address.account_public_key, tx->address_type);
              break;
            case GRDT_TRANSACTION_DEFERRED_TRANSFER:
              note_type(tx->transfer.recipient_pubkey, GRDT_ADDRESS_DEFERRED_TRANSFER);
              break;
            default:
              break;
            }
            for (size_t i = 0; i < tx->account_balances_count; ++i) {
              Key key{};
              memcpy(key.data(), tx->account_balances[i].pubkey, SIGN_PUBLIC_KEY_SIZE);
              if (key == zero) { continue; }
              (*fill.reference)[key].last_balance = tx->tx_nr;
            }
            return ARNM_SUCCESS;
          },
          &fill, nullptr
      ),
      ARNM_SUCCESS
  );

  ASSERT_GT(fill.seen, 0u);
  if (!source.path) { EXPECT_EQ(fill.seen, source.count); }
  EXPECT_EQ(grdb_addresses_size(&index.index), reference.size());
  for (const auto &entry : reference) {
    const Key &key = entry.first;
    const Address &address = entry.second;
    EXPECT_EQ(
        grdb_addresses_type(&index.index, key.data()),
        address.types.empty() ? GRDT_ADDRESS_NONE : address.types.back().second
    );
    for (uint64_t at : {uint64_t{0}, fill.max_tx / 3u, fill.max_tx}) {
      grdt_address want = GRDT_ADDRESS_NONE;
      const bool wanted = TypeAt(address, at, &want);
      grdt_address got = GRDT_ADDRESS_NONE;
      const bool found = grdb_addresses_type_at(&index.index, key.data(), at, &got);
      ASSERT_EQ(found, wanted) << "at " << at;
      if (found) { EXPECT_EQ(got, want) << "at " << at; }
    }
    uint64_t balance_tx = 0;
    const bool has_balance = grdb_addresses_last_balance(&index.index, key.data(), &balance_tx);
    EXPECT_EQ(has_balance, address.last_balance != 0);
    if (has_balance) { EXPECT_EQ(balance_tx, address.last_balance); }
  }
}

std::string ChainPath() {
  if (const char *path = std::getenv("GRD_BENCH_CHAIN_DATA")) { return path; }
  return "../gradido_blockchain/build/tests/data/blk00000001.dat";
}

TEST(AddressIndex, ARealChainMatchesTheReference) {
  const std::string path = ChainPath();
  FILE *file = fopen(path.c_str(), "rb");
  if (!file) { GTEST_SKIP() << "no chain file at " << path; }

  Index index;
  Reference reference;
  uint64_t max_tx = 0;
  alignas(8) static uint8_t decode_buffer[256u * 1024u];
  static uint8_t tx_buffer[65535u];
  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);
  uint8_t size_bytes[2];
  const Key zero{};
  while (2u == fread(size_bytes, 1, 2, file)) {
    const uint16_t size = static_cast<uint16_t>(size_bytes[0] | (size_bytes[1] << 8));
    if (!size) { continue; }
    if (size != fread(tx_buffer, 1, size, file)) { break; }
    if (ARNM_SUCCESS !=
        grdr_complete_transaction_init_from_protobuf(
            &tx, tx_buffer, size, bench_default_community_uuid, decode_buffer, sizeof(decode_buffer)
        )) {
      continue;
    }
    ASSERT_EQ(grdb_addresses_add(&index.index, &tx), ARNM_SUCCESS) << "tx " << tx.tx_nr;
    max_tx = tx.tx_nr;

    auto note_type = [&](const uint8_t *key_bytes, grdt_address type) {
      if (!key_bytes) { return; }
      Key key{};
      memcpy(key.data(), key_bytes, SIGN_PUBLIC_KEY_SIZE);
      if (key == zero) { return; }
      reference[key].types.push_back({tx.tx_nr, type});
    };
    switch (tx.transaction_type) {
    case GRDT_TRANSACTION_COMMUNITY_ROOT:
      note_type(tx.community_root.gmw_public_key, GRDT_ADDRESS_COMMUNITY_GMW);
      note_type(tx.community_root.auf_public_key, GRDT_ADDRESS_COMMUNITY_AUF);
      break;
    case GRDT_TRANSACTION_REGISTER_ADDRESS:
      note_type(tx.register_address.user_public_key, tx.address_type);
      note_type(tx.register_address.account_public_key, tx.address_type);
      break;
    case GRDT_TRANSACTION_DEFERRED_TRANSFER:
      note_type(tx.transfer.recipient_pubkey, GRDT_ADDRESS_DEFERRED_TRANSFER);
      break;
    default:
      break;
    }
    for (size_t i = 0; i < tx.account_balances_count; ++i) {
      Key key{};
      memcpy(key.data(), tx.account_balances[i].pubkey, SIGN_PUBLIC_KEY_SIZE);
      if (key == zero) { continue; }
      reference[key].last_balance = tx.tx_nr;
    }
    grdr_complete_transaction_release(&tx);
  }
  fclose(file);
  ASSERT_GT(reference.size(), 100u) << "the file holds no transactions this build can read";
  EXPECT_EQ(grdb_addresses_size(&index.index), reference.size());

  // every address, asked at four points of the chain rather than at every number
  for (const auto &entry : reference) {
    const Key &key = entry.first;
    const Address &address = entry.second;
    EXPECT_EQ(
        grdb_addresses_type(&index.index, key.data()),
        address.types.empty() ? GRDT_ADDRESS_NONE : address.types.back().second
    );
    for (uint64_t at : {uint64_t{0}, max_tx / 3u, max_tx / 2u, max_tx}) {
      grdt_address want = GRDT_ADDRESS_NONE;
      const bool wanted = TypeAt(address, at, &want);
      grdt_address got = GRDT_ADDRESS_NONE;
      const bool found = grdb_addresses_type_at(&index.index, key.data(), at, &got);
      ASSERT_EQ(found, wanted) << "at " << at;
      if (found) { EXPECT_EQ(got, want) << "at " << at; }
    }
    uint64_t balance_tx = 0;
    const bool has_balance = grdb_addresses_last_balance(&index.index, key.data(), &balance_tx);
    EXPECT_EQ(has_balance, address.last_balance != 0);
    if (has_balance) { EXPECT_EQ(balance_tx, address.last_balance); }
  }
}

// ********** a refusal part way, and the add that follows it *******************

/*
 * An add that runs out of memory part way leaves what it had already written, and the caller
 * adds the transaction again once there is room -- that is the contract grdb_chain_add builds
 * on. These tests run the arena dry at exactly the step that matters, give the room back, and
 * check that the second add finishes the first rather than repeating it.
 *
 * How the arena is run dry and given its room back is in starved_arena.h.
 */

/** 4096 and more distinct keys; MakeKey wraps after 256. */
Key ManyKey(uint32_t n) {
  Key key{};
  key[0] = 0xA7;
  key[1] = static_cast<uint8_t>(n >> 24);
  key[2] = static_cast<uint8_t>(n >> 16);
  key[3] = static_cast<uint8_t>(n >> 8);
  key[4] = static_cast<uint8_t>(n);
  key[31] = 0x5C;
  return key;
}

grdr_complete_transaction Deferred(uint64_t tx_nr, const Key &recipient) {
  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);
  tx.tx_nr = tx_nr;
  tx.transaction_type = GRDT_TRANSACTION_DEFERRED_TRANSFER;
  memcpy(tx.transfer.recipient_pubkey, recipient.data(), SIGN_PUBLIC_KEY_SIZE);
  return tx;
}

TEST(AddressIndex, ARetryAfterARefusalPartWayWritesNoTypeChangeTwice) {
  Starved memory(4u * 1024u * 1024u);
  grdb_addresses index;
  ASSERT_EQ(grdb_addresses_init(&index, nullptr, &memory.arena), ARNM_SUCCESS);

  const Key user = ManyKey(1);
  const Key account = ManyKey(2);
  const Key filler = ManyKey(3);
  uint64_t tx_nr = 0;

  // both keys already known, so typing them later allocates no entry -- only a record
  grdr_complete_transaction tx = Deferred(++tx_nr, user);
  ASSERT_EQ(grdb_addresses_add(&index, &tx), ARNM_SUCCESS);
  tx = Deferred(++tx_nr, account);
  ASSERT_EQ(grdb_addresses_add(&index, &tx), ARNM_SUCCESS);

  // exactly one record left in the newest bucket: the user key's fits, the account key's not
  const uint32_t per_bucket = 1u << index.type_changes.bucket_capacity_max_log2;
  while (index.type_changes.tail_used != per_bucket - 1u) {
    tx = Deferred(++tx_nr, filler);
    ASSERT_EQ(grdb_addresses_add(&index, &tx), ARNM_SUCCESS);
  }

  const uint64_t registration = ++tx_nr;
  grdr_complete_transaction reg;
  grdr_complete_transaction_init(&reg);
  reg.tx_nr = registration;
  reg.transaction_type = GRDT_TRANSACTION_REGISTER_ADDRESS;
  reg.address_type = GRDT_ADDRESS_COMMUNITY_HUMAN;
  memcpy(reg.register_address.user_public_key, user.data(), SIGN_PUBLIC_KEY_SIZE);
  memcpy(reg.register_address.account_public_key, account.data(), SIGN_PUBLIC_KEY_SIZE);

  memory.starve(8);
  const uint32_t before = arnm_bvec_size(&index.type_changes);
  ASSERT_EQ(grdb_addresses_add(&index, &reg), ARNM_ERROR_OUT_OF_MEMORY);
  ASSERT_EQ(arnm_bvec_size(&index.type_changes), before + 1u)
      << "the state this test is about: the user key typed, the account key not";

  memory.feed();
  ASSERT_EQ(grdb_addresses_add(&index, &reg), ARNM_SUCCESS) << "the second add finishes the first";

  uint64_t changes[4] = {0};
  ASSERT_EQ(grdb_addresses_type_changes(&index, user.data(), changes, 4), 2u)
      << "the deferred transfer and the registration, each once";
  EXPECT_EQ(changes[0], registration);
  EXPECT_EQ(changes[1], 1u);
  ASSERT_EQ(grdb_addresses_type_changes(&index, account.data(), changes, 4), 2u);
  EXPECT_EQ(changes[0], registration);
  EXPECT_EQ(grdb_addresses_type(&index, user.data()), GRDT_ADDRESS_COMMUNITY_HUMAN);
  EXPECT_EQ(grdb_addresses_type(&index, account.data()), GRDT_ADDRESS_COMMUNITY_HUMAN);

  grdb_addresses_release(&index);
}

TEST(AddressIndex, AKeyTheMapTookWithoutItsEntryIsNotRefusedForGood) {
  Starved memory(4u * 1024u * 1024u);
  grdb_addresses index;
  ASSERT_EQ(grdb_addresses_init(&index, nullptr, &memory.arena), ARNM_SUCCESS);

  // One new key per transaction until the newest bucket of entries is full. Key buckets and
  // entry buckets fill in step here -- a full entry bucket is always a full key bucket too -- so
  // the map is given its room up front: the one allocation left to fail is then the entry's,
  // and the block that starves the arena stays its newest, which is what lets it be given back.
  const uint32_t per_bucket = 1u << index.entries.bucket_capacity_max_log2;
  ASSERT_EQ(arnm_key_map_reserve(&index.keys, 2u * per_bucket), ARNM_SUCCESS);
  uint64_t tx_nr = 0;
  uint32_t key_number = 100;
  std::vector<grdw_account_balance> balance(1);
  grdr_complete_transaction tx;
  do {
    grdr_complete_transaction_init(&tx);
    tx.tx_nr = ++tx_nr;
    tx.transaction_type = GRDT_TRANSACTION_TRANSFER;
    balance[0] = grdw_account_balance{};
    const Key key = ManyKey(key_number++);
    memcpy(balance[0].pubkey, key.data(), SIGN_PUBLIC_KEY_SIZE);
    tx.account_balances = balance.data();
    tx.account_balances_count = 1;
    ASSERT_EQ(grdb_addresses_add(&index, &tx), ARNM_SUCCESS);
  } while (index.entries.tail_used != per_bucket);

  // a new key: the map takes it, the entry wants a fresh bucket and finds less than one
  const Key fresh = ManyKey(key_number++);
  grdr_complete_transaction_init(&tx);
  tx.tx_nr = ++tx_nr;
  tx.transaction_type = GRDT_TRANSACTION_TRANSFER;
  balance[0] = grdw_account_balance{};
  memcpy(balance[0].pubkey, fresh.data(), SIGN_PUBLIC_KEY_SIZE);
  tx.account_balances = balance.data();
  tx.account_balances_count = 1;

  memory.starve(8);
  ASSERT_EQ(grdb_addresses_add(&index, &tx), ARNM_ERROR_OUT_OF_MEMORY);
  ASSERT_EQ(arnm_key_map_size(&index.keys), arnm_bvec_size(&index.entries) + 1u)
      << "the state this test is about: a key in the map, no entry behind it";

  memory.feed();
  ASSERT_EQ(grdb_addresses_add(&index, &tx), ARNM_SUCCESS)
      << "with room again, the key the map already holds gets its entry";
  uint64_t last = 0;
  ASSERT_TRUE(grdb_addresses_last_balance(&index, fresh.data(), &last));
  EXPECT_EQ(last, tx_nr);
  EXPECT_EQ(arnm_key_map_size(&index.keys), arnm_bvec_size(&index.entries));

  grdb_addresses_release(&index);
}

} // namespace
