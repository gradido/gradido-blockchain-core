#include <gtest/gtest.h>

#include "arnm/arena.h"
#include "arnm/memory.h"

#include "gradido_blockchain_core/blockchain/chain.h"
#include "gradido_blockchain_core/blockchain/ffi.h"
#include "gradido_blockchain_core/blockchain/in_memory.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"

#include "memory_limit.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/*
 * The store: where a chain's transactions are, behind two function pointers.
 *
 * What is really being tested is that nothing is copied and nothing is decoded twice. A
 * grdr_complete_transaction carries its own allocator handle, so it moves and is never copied;
 * a store takes one by moving it in and answers a fetch with its address. These tests watch the
 * addresses, and count what a host is asked, because that is where a needless decode would show.
 *
 * The transactions are real: the confirmed community root the protobuf tests use, decoded once
 * into its wire form and encoded again with a fresh number and day for every one that is needed.
 */

namespace {

constexpr auto confirmedCommunityRootTransactionBase64 =
    "CHkS3gEKZgpkCiCBZwMplGmI7fRR9MQkaR2Dz1qQQ5BCiC1btyJD71Ue9BJAtT7yJ8kBub5BCxCDG5wZ8s/"
    "dFKf2ystCXQQc4lZnkZmffBwTO6Udq5LupPfaAbvyFIt7942U+"
    "kHiuF52wokQChJ0WmYKIIFnAymUaYjt9FH0xCRpHYPPWpBDkEKILVu3IkPvVR70EiDX46igkKpEhzJG9cas/Bf/"
    "dO4XT1bnvSpV/7gQQfbbHRogrYcHSiqkvALTeX+7Q8iyvm7dbLHWNEqUD8UovOHhMLISBgiAzLn/BRiIgAwaBgjC8rn/"
    "BSCIgAwqIGHF/azvYntEu9pwC3bmSL61/"
    "Ob0pLcCTWspFwaJb4Q7MhQaEAoKCIDMuf8FELeVERICGHkIAjo3CiDbDtYSWhTwMKvtG/"
    "yDHgohjPn6v87n7NWBwMDniPAXxxCQThoQAZ4sMaMDdcCUHvNcWeT5eA==";

/** The community the fixture's balances are counted in. */
const uint8_t kCommunityUuid[ARNM_UUID_BINARY_SIZE] = {0x01, 0x9e, 0x2c, 0x31, 0xa3, 0x03,
                                                       0x75, 0xc0, 0x94, 0x1e, 0xf3, 0x5c,
                                                       0x59, 0xe4, 0xf9, 0x78};

/** Serialized transactions numbered 1..count, each a day after the one before. */
class Serialized {
public:
  explicit Serialized(uint32_t count) {
    const size_t characters = std::strlen(confirmedCommunityRootTransactionBase64);
    std::vector<uint8_t> raw(ARNM_BASE64_BINARY_SIZE(characters) + 8u, 0u);
    uint32_t raw_size = 0;
    EXPECT_EQ(
        ARNM_SUCCESS,
        arnm_binary_from_base64(raw.data(), &raw_size, confirmedCommunityRootTransactionBase64)
    );

    arnm arena{};
    EXPECT_EQ(ARNM_SUCCESS, arnm_init_arena(&arena, 512u * 1024u));
    const arnm_memory_block source_block = {raw.data(), raw_size};
    grdw_confirmed_transaction wire;
    grdw_confirmed_transaction_init(&wire);
    EXPECT_EQ(ARNM_SUCCESS, grdw_confirmed_transaction_decode(&wire, &source_block, &arena));

    const int64_t first_second = wire.confirmed_at.seconds;
    for (uint32_t i = 1; i <= count; ++i) {
      wire.id = i;
      wire.confirmed_at.seconds = first_second + static_cast<int64_t>(i) * 86400;
      std::vector<uint8_t> room(raw_size + 64u, 0u);
      arnm_memory_block destination = {room.data(), static_cast<uint32_t>(room.size())};
      int written = 0;
      EXPECT_EQ(
          ARNM_SUCCESS, grdw_confirmed_transaction_encode(&destination, &written, &wire, &arena)
      );
      EXPECT_GT(written, 0);
      room.resize(static_cast<size_t>(written));
      bytes_.push_back(std::move(room));
    }
    arnm_release(&arena);
  }

  uint32_t size() const {
    return static_cast<uint32_t>(bytes_.size());
  }

  arnm_memory_block block(uint32_t index) const {
    return arnm_memory_block{
        const_cast<uint8_t *>(bytes_[index].data()), static_cast<uint32_t>(bytes_[index].size())
    };
  }

private:
  std::vector<std::vector<uint8_t>> bytes_;
};

/** One transaction of one's own, decoded from a block, to be moved into a store. */
class Own {
public:
  Own() {
    grdr_complete_transaction_init(&tx_);
  }
  ~Own() {
    grdr_complete_transaction_release(&tx_);
  }
  Own(const Own &) = delete;
  Own &operator=(const Own &) = delete;

  void decode(const Serialized &source, uint32_t index) {
    static std::vector<uint64_t> scratch(32u * 1024u);
    const arnm_memory_block block = source.block(index);
    ASSERT_EQ(
        ARNM_SUCCESS, grdr_complete_transaction_init_from_protobuf(
                          &tx_, block.data, block.size, kCommunityUuid,
                          reinterpret_cast<uint8_t *>(scratch.data()),
                          static_cast<uint32_t>(scratch.size() * sizeof(uint64_t))
                      )
    );
  }

  grdr_complete_transaction *get() {
    return &tx_;
  }
  bool empty() const {
    return tx_.tx_nr == 0 && tx_.account_balances == nullptr;
  }

private:
  grdr_complete_transaction tx_{};
};

/** An in memory store, filled by moving decoded transactions in. */
class Moved {
public:
  explicit Moved(uint32_t count) : source_(count) {
    EXPECT_EQ(ARNM_SUCCESS, grdb_in_memory_init(&store_, nullptr, kCommunityUuid, nullptr));
    wrapped_ = grdb_in_memory_as_store(&store_);
    for (uint32_t i = 0; i < count; ++i) {
      Own own;
      own.decode(source_, i);
      EXPECT_EQ(ARNM_SUCCESS, grdb_chain_store_append(&wrapped_, i + 1u, own.get(), nullptr));
      EXPECT_TRUE(own.empty()) << "append takes the transaction; the caller is left empty";
    }
  }
  ~Moved() {
    grdb_in_memory_release(&store_);
  }
  Moved(const Moved &) = delete;
  Moved &operator=(const Moved &) = delete;

  grdb_chain_store &as_store() {
    return wrapped_;
  }
  grdb_in_memory &store() {
    return store_;
  }
  const Serialized &source() const {
    return source_;
  }

private:
  Serialized source_;
  grdb_in_memory store_{};
  grdb_chain_store wrapped_{};
};

// ********** the store on its own *******************

TEST(InMemoryStore, TakesATransactionAndHandsBackItsAddress) {
  Moved filled(8);
  EXPECT_EQ(8u, grdb_in_memory_size(&filled.store()));

  for (uint64_t number = 1; number <= 8; ++number) {
    const grdr_complete_transaction *tx = nullptr;
    ASSERT_EQ(ARNM_SUCCESS, grdb_chain_store_fetch(&filled.as_store(), number, &tx));
    ASSERT_NE(nullptr, tx);
    EXPECT_EQ(number, tx->tx_nr);
    EXPECT_EQ(GRDT_TRANSACTION_COMMUNITY_ROOT, tx->transaction_type);
    EXPECT_EQ(tx, grdb_in_memory_at(&filled.store(), number));
  }
}

TEST(InMemoryStore, TheMoveKeepsEverythingTheArenaHolds) {
  // The whole design rests on this: the struct travels, the arena it points into does not, and
  // what lived in that arena reads back byte for byte on the other side.
  Serialized source(1);
  Own own;
  own.decode(source, 0);
  ASSERT_GT(own.get()->account_balances_count, 0u);

  const size_t balances = own.get()->account_balances_count;
  const uint8_t *arena_address = reinterpret_cast<const uint8_t *>(own.get()->account_balances);
  std::vector<uint8_t> before(
      arena_address, arena_address + balances * sizeof(grdw_account_balance)
  );
  const uint8_t *body_before = own.get()->body_bytes.data;
  const uint32_t body_size = own.get()->body_bytes.size;
  std::vector<uint8_t> body(body_before, body_before + body_size);

  grdb_in_memory store;
  ASSERT_EQ(ARNM_SUCCESS, grdb_in_memory_init(&store, nullptr, kCommunityUuid, nullptr));
  grdb_chain_store wrapped = grdb_in_memory_as_store(&store);
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_store_append(&wrapped, 1, own.get(), nullptr));
  EXPECT_TRUE(own.empty());

  const grdr_complete_transaction *kept = grdb_in_memory_at(&store, 1);
  ASSERT_NE(nullptr, kept);
  EXPECT_EQ(balances, kept->account_balances_count);
  EXPECT_EQ(arena_address, reinterpret_cast<const uint8_t *>(kept->account_balances))
      << "the arena stayed where it was; only the handle travelled";
  EXPECT_EQ(0, std::memcmp(kept->account_balances, before.data(), before.size()))
      << "and what stood in it is unchanged";
  EXPECT_EQ(body_before, kept->body_bytes.data);
  EXPECT_EQ(body_size, kept->body_bytes.size);
  EXPECT_EQ(0, std::memcmp(kept->body_bytes.data, body.data(), body.size()));

  grdb_in_memory_release(&store);
}

TEST(InMemoryStore, TheAddressIsTheStoresAndDoesNotMoveOrGoStale) {
  Moved filled(6);
  const grdr_complete_transaction *first = nullptr;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_store_fetch(&filled.as_store(), 2, &first));
  const uint8_t *balances = reinterpret_cast<const uint8_t *>(first->account_balances);

  // reading every other transaction in between must not disturb the one already in hand
  for (uint64_t number = 1; number <= 6; ++number) {
    const grdr_complete_transaction *other = nullptr;
    ASSERT_EQ(ARNM_SUCCESS, grdb_chain_store_fetch(&filled.as_store(), number, &other));
    EXPECT_NE(first, other == first ? nullptr : other) << "each transaction has its own place";
  }

  const grdr_complete_transaction *again = nullptr;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_store_fetch(&filled.as_store(), 2, &again));
  EXPECT_EQ(first, again) << "the same number gives the same address, not a fresh decode";
  EXPECT_EQ(2u, again->tx_nr);
  EXPECT_EQ(balances, reinterpret_cast<const uint8_t *>(again->account_balances))
      << "what it points at did not move either";
}

TEST(InMemoryStore, BytesAreTheOtherWayInAndAreDecodedOnce) {
  Serialized source(4);
  grdb_in_memory store;
  ASSERT_EQ(ARNM_SUCCESS, grdb_in_memory_init(&store, nullptr, kCommunityUuid, nullptr));
  grdb_chain_store wrapped = grdb_in_memory_as_store(&store);

  for (uint32_t i = 0; i < 4; ++i) {
    const arnm_memory_block block = source.block(i);
    // no object to hand over, only bytes: the store decodes them on the way in
    ASSERT_EQ(ARNM_SUCCESS, grdb_chain_store_append(&wrapped, i + 1u, nullptr, &block));
  }
  EXPECT_EQ(4u, grdb_in_memory_size(&store));

  const grdr_complete_transaction *tx = nullptr;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_store_fetch(&wrapped, 3, &tx));
  EXPECT_EQ(3u, tx->tx_nr);
  EXPECT_EQ(tx, grdb_in_memory_at(&store, 3)) << "kept as an object, not as bytes";

  grdb_in_memory_release(&store);
}

TEST(InMemoryStore, ANumberItDoesNotHoldIsAnAnswerNotAFailure) {
  Moved filled(4);
  const grdr_complete_transaction *tx = nullptr;
  EXPECT_EQ(
      ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS, grdb_chain_store_fetch(&filled.as_store(), 0, &tx)
  );
  EXPECT_EQ(
      ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS, grdb_chain_store_fetch(&filled.as_store(), 5, &tx)
  );
  EXPECT_EQ(nullptr, tx) << "a refused fetch leaves the pointer alone";
  EXPECT_EQ(nullptr, grdb_in_memory_at(&filled.store(), 99));
}

TEST(InMemoryStore, KeepsARunOfNumbersWithoutGaps) {
  Moved filled(3);
  Own own;
  own.decode(filled.source(), 0);
  EXPECT_EQ(
      ARNM_ERROR_INVALID_PARAM, grdb_chain_store_append(&filled.as_store(), 5, own.get(), nullptr)
  ) << "a number that skips one is refused";
  EXPECT_FALSE(own.empty()) << "and a refusal leaves the transaction with its caller";
  EXPECT_EQ(
      ARNM_ERROR_INVALID_PARAM, grdb_chain_store_append(&filled.as_store(), 3, own.get(), nullptr)
  ) << "a number already kept is refused";
  EXPECT_EQ(ARNM_SUCCESS, grdb_chain_store_append(&filled.as_store(), 4, own.get(), nullptr));
  EXPECT_EQ(4u, grdb_in_memory_size(&filled.store()));
}

TEST(InMemoryStore, ResetReleasesWhatItTookAndItFillsAgain) {
  Moved filled(3);
  grdb_in_memory_reset(&filled.store());
  EXPECT_EQ(0u, grdb_in_memory_size(&filled.store()));
  Own own;
  own.decode(filled.source(), 0);
  EXPECT_EQ(ARNM_SUCCESS, grdb_chain_store_append(&filled.as_store(), 1, own.get(), nullptr));
  EXPECT_EQ(1u, grdb_in_memory_size(&filled.store()));
}

// ********** a chain reading through a store *******************

TEST(ChainStore, LoadRebuildsTheIndexFromTheChain) {
  Moved filled(40);
  grdb_chain_options options = {};
  options.segment_transactions = 7; // several segments, so load crosses their borders
  options.store = &filled.as_store();

  grdb_chain chain;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_init(&chain, &options, nullptr));

  uint64_t loaded = 0;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_load(&chain, 0, 0, &loaded));
  EXPECT_EQ(40u, loaded) << "0 and 0 means: from the first, until the store has no more";
  EXPECT_EQ(40u, grdb_chain_size(&chain));
  EXPECT_EQ(6u, grdb_chain_segment_count(&chain));

  const grdb_transactions_filter everything = {};
  uint64_t count = 0;
  EXPECT_EQ(ARNM_SUCCESS, grdb_chain_count(&chain, &everything, &count));
  EXPECT_EQ(40u, count);

  uint64_t newest = 0;
  EXPECT_TRUE(grdb_chain_newest(&chain, &everything, &newest));
  EXPECT_EQ(40u, newest);

  grdb_chain_release(&chain);
}

TEST(ChainStore, LoadTakesARangeAndSaysHowFarItGot) {
  Moved filled(20);
  grdb_chain_options options = {};
  options.store = &filled.as_store();

  grdb_chain chain;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_init(&chain, &options, nullptr));

  uint64_t loaded = 0;
  EXPECT_EQ(ARNM_SUCCESS, grdb_chain_load(&chain, 1, 5, &loaded));
  EXPECT_EQ(5u, loaded);
  EXPECT_EQ(ARNM_SUCCESS, grdb_chain_load(&chain, 6, 20, &loaded));
  EXPECT_EQ(15u, loaded);
  EXPECT_EQ(20u, grdb_chain_size(&chain));

  EXPECT_EQ(ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS, grdb_chain_load(&chain, 21, 25, &loaded));
  EXPECT_EQ(0u, loaded);
  EXPECT_EQ(ARNM_ERROR_INVALID_PARAM, grdb_chain_load(&chain, 10, 4, &loaded));

  grdb_chain_release(&chain);
}

TEST(ChainStore, FetchIsWhatTheIndexNeverAnswers) {
  Moved filled(6);
  grdb_chain_options options = {};
  options.store = &filled.as_store();

  grdb_chain chain;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_init(&chain, &options, nullptr));
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_load(&chain, 0, 0, nullptr));

  const grdr_complete_transaction *tx = nullptr;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_fetch(&chain, 4, &tx));
  EXPECT_EQ(4u, tx->tx_nr);
  EXPECT_EQ(ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS, grdb_chain_fetch(&chain, 99, &tx));
  EXPECT_TRUE(grdb_chain_is_writable(&chain));

  grdb_chain_release(&chain);
}

TEST(ChainStore, AChainWithoutAStoreIsAnIndexAndSaysSo) {
  grdb_chain chain;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_init(&chain, nullptr, nullptr));

  const grdr_complete_transaction *tx = nullptr;
  EXPECT_EQ(ARNM_ERROR_INVALID_STATE, grdb_chain_fetch(&chain, 1, &tx));
  EXPECT_EQ(ARNM_ERROR_INVALID_STATE, grdb_chain_load(&chain, 0, 0, nullptr));
  EXPECT_FALSE(grdb_chain_is_writable(&chain));
  grdb_chain_release(&chain);
}

// ********** the same chain through a host's callbacks *******************

/** A host answering out of a Serialized, counting what it was asked. */
struct Host {
  const Serialized *source = nullptr;
  uint32_t gets = 0;
  uint32_t releases = 0;
  uint32_t puts = 0;
  bool break_the_next_get = false;

  static int Get(void *user_data, uint64_t tx_nr, const uint8_t **data, uint32_t *size) {
    Host *host = static_cast<Host *>(user_data);
    ++host->gets;
    if (host->break_the_next_get) {
      host->break_the_next_get = false;
      return 42; // neither OK nor NOT_FOUND: the lookup itself went wrong
    }
    if (!tx_nr || tx_nr > host->source->size()) { return GRDB_FFI_NOT_FOUND; }
    const arnm_memory_block block = host->source->block(static_cast<uint32_t>(tx_nr - 1u));
    *data = block.data;
    *size = block.size;
    return GRDB_FFI_OK;
  }

  static void Release(void *user_data, const uint8_t *, uint32_t) {
    ++static_cast<Host *>(user_data)->releases;
  }

  static int Put(void *user_data, uint64_t, const uint8_t *, uint32_t) {
    ++static_cast<Host *>(user_data)->puts;
    return GRDB_FFI_OK;
  }
};

TEST(ChainFfi, AChainTheHostHoldsReadsLikeAnyOther) {
  Serialized source(25);
  Host host;
  host.source = &source;

  grdb_chain_ffi_host callbacks = {};
  callbacks.user_data = &host;
  callbacks.get = Host::Get;
  callbacks.release = Host::Release;
  callbacks.put = Host::Put;

  grdb_chain_ffi adapter;
  ASSERT_EQ(
      ARNM_SUCCESS, grdb_chain_ffi_init(&adapter, &callbacks, nullptr, kCommunityUuid, nullptr)
  );

  grdb_chain_store store = grdb_chain_ffi_as_store(&adapter);
  grdb_chain_options options = {};
  options.segment_transactions = 8;
  options.store = &store;
  grdb_chain chain;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_init(&chain, &options, nullptr));

  uint64_t loaded = 0;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_load(&chain, 0, 0, &loaded));
  EXPECT_EQ(25u, loaded);
  EXPECT_EQ(25u, grdb_chain_size(&chain));
  EXPECT_EQ(4u, grdb_chain_segment_count(&chain));

  // 25 handed over, one more asked for that was not there
  EXPECT_EQ(26u, host.gets);
  EXPECT_EQ(25u, host.releases) << "release comes once per handover, and only per handover";

  grdb_chain_release(&chain);
  grdb_chain_ffi_release(&adapter);
}

TEST(ChainFfi, ItDecodesIntoOnePlaceAndNotTwiceForTheSameNumber) {
  Serialized source(5);
  Host host;
  host.source = &source;

  grdb_chain_ffi_host callbacks = {};
  callbacks.user_data = &host;
  callbacks.get = Host::Get;
  callbacks.release = Host::Release;

  grdb_chain_ffi adapter;
  ASSERT_EQ(
      ARNM_SUCCESS, grdb_chain_ffi_init(&adapter, &callbacks, nullptr, kCommunityUuid, nullptr)
  );
  grdb_chain_store store = grdb_chain_ffi_as_store(&adapter);

  const grdr_complete_transaction *first = nullptr;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_store_fetch(&store, 3, &first));
  EXPECT_EQ(3u, first->tx_nr);
  EXPECT_EQ(1u, host.gets);

  const grdr_complete_transaction *again = nullptr;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_store_fetch(&store, 3, &again));
  EXPECT_EQ(first, again);
  EXPECT_EQ(1u, host.gets) << "the same number twice costs one decode, not two";

  const grdr_complete_transaction *other = nullptr;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_store_fetch(&store, 4, &other));
  EXPECT_EQ(2u, host.gets);
  EXPECT_EQ(first, other) << "one place to decode into, so the address is the same";
  EXPECT_EQ(4u, other->tx_nr) << "and what stands in it is the number just asked for";

  grdb_chain_ffi_release(&adapter);
}

TEST(ChainFfi, TheHostsAnswersAreTranslatedNotGuessedAt) {
  Serialized source(3);
  Host host;
  host.source = &source;

  grdb_chain_ffi_host callbacks = {};
  callbacks.user_data = &host;
  callbacks.get = Host::Get;
  callbacks.release = Host::Release;

  grdb_chain_ffi adapter;
  ASSERT_EQ(
      ARNM_SUCCESS, grdb_chain_ffi_init(&adapter, &callbacks, nullptr, kCommunityUuid, nullptr)
  );
  grdb_chain_store store = grdb_chain_ffi_as_store(&adapter);

  const grdr_complete_transaction *tx = nullptr;
  EXPECT_EQ(ARNM_SUCCESS, grdb_chain_store_fetch(&store, 2, &tx));
  EXPECT_EQ(2u, tx->tx_nr);

  EXPECT_EQ(ARNM_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS, grdb_chain_store_fetch(&store, 9, &tx))
      << "GRDB_FFI_NOT_FOUND is an answer";
  EXPECT_EQ(1u, host.releases) << "nothing was handed over, so nothing is released";

  host.break_the_next_get = true;
  EXPECT_EQ(ARNM_ERROR_DECODE_FAILED, grdb_chain_store_fetch(&store, 1, &tx))
      << "any other value is a failure";

  EXPECT_FALSE(grdb_chain_store_is_writable(&store)) << "a host without a put is read only";
  const arnm_memory_block block = source.block(0);
  EXPECT_EQ(ARNM_ERROR_INVALID_STATE, grdb_chain_store_append(&store, 4, nullptr, &block));
  EXPECT_EQ(0u, host.puts);

  grdb_chain_ffi_release(&adapter);
}

TEST(ChainFfi, AnAppendNeedsBytesAndKeepsTheObject) {
  Serialized source(2);
  Host host;
  host.source = &source;

  grdb_chain_ffi_host callbacks = {};
  callbacks.user_data = &host;
  callbacks.get = Host::Get;
  callbacks.put = Host::Put;

  grdb_chain_ffi adapter;
  ASSERT_EQ(
      ARNM_SUCCESS, grdb_chain_ffi_init(&adapter, &callbacks, nullptr, kCommunityUuid, nullptr)
  );
  grdb_chain_store store = grdb_chain_ffi_as_store(&adapter);

  Own own;
  own.decode(source, 0);
  EXPECT_EQ(ARNM_ERROR_INVALID_PARAM, grdb_chain_store_append(&store, 1, own.get(), nullptr))
      << "this store writes bytes down; an object alone is nothing it could keep";
  EXPECT_FALSE(own.empty());

  const arnm_memory_block block = source.block(0);
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_store_append(&store, 1, own.get(), &block));
  EXPECT_TRUE(own.empty()) << "taken";
  EXPECT_EQ(1u, host.puts);

  const grdr_complete_transaction *back = nullptr;
  ASSERT_EQ(ARNM_SUCCESS, grdb_chain_store_fetch(&store, 1, &back));
  EXPECT_EQ(1u, back->tx_nr);
  EXPECT_EQ(0u, host.gets) << "what was just written is read back without asking the host";

  grdb_chain_ffi_release(&adapter);
}

TEST(ChainFfi, WhatItRefuses) {
  grdb_chain_ffi adapter;
  grdb_chain_ffi_host callbacks = {};
  EXPECT_EQ(
      ARNM_ERROR_NULL_POINTER,
      grdb_chain_ffi_init(&adapter, &callbacks, nullptr, kCommunityUuid, nullptr)
  ) << "a host without a get is no store";
  callbacks.get = Host::Get;
  EXPECT_EQ(
      ARNM_ERROR_NULL_POINTER, grdb_chain_ffi_init(&adapter, &callbacks, nullptr, nullptr, nullptr)
  );
  EXPECT_EQ(
      ARNM_ERROR_NULL_POINTER,
      grdb_chain_ffi_init(nullptr, &callbacks, nullptr, kCommunityUuid, nullptr)
  );

  const grdb_chain_store empty = grdb_chain_ffi_as_store(nullptr);
  EXPECT_FALSE(grdb_chain_store_is_readable(&empty));
  EXPECT_FALSE(grdb_chain_store_is_writable(&empty));

  const grdr_complete_transaction *tx = nullptr;
  EXPECT_EQ(ARNM_ERROR_INVALID_STATE, grdb_chain_store_fetch(&empty, 1, &tx));
  EXPECT_EQ(ARNM_ERROR_NULL_POINTER, grdb_chain_store_fetch(&empty, 1, nullptr));
  EXPECT_EQ(ARNM_ERROR_NULL_POINTER, grdb_chain_store_append(&empty, 1, nullptr, nullptr));

  grdb_chain_ffi_release(nullptr);
  grdb_in_memory_release(nullptr);
  grdb_in_memory_reset(nullptr);
  EXPECT_EQ(0u, grdb_in_memory_size(nullptr));
  EXPECT_EQ(nullptr, grdb_in_memory_at(nullptr, 1));
}

} // namespace
