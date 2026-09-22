#include <gtest/gtest.h>

#include "gradido_blockchain_core/blockchain/transactions.h"
#include "gradido_blockchain_core/blockchain/transactions_filter.h"

#include "memory_limit.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

/*
 * The filter on its own: what the setters write, what they refuse, and what a reader gets back.
 * Plus the one thing that matters across an FFI boundary -- a filter built through the calls
 * answers exactly as one written field by field, and it keeps no pointer into the caller's
 * memory, so a key that is gone by the time the question is asked changes nothing.
 */

namespace {

using Key = std::array<uint8_t, SIGN_PUBLIC_KEY_SIZE>;
using Uuid = std::array<uint8_t, ARNM_UUID_BINARY_SIZE>;

Key MakeKey(uint32_t n) {
  Key key{};
  for (size_t i = 0; i < key.size(); ++i) { key[i] = static_cast<uint8_t>(n * 13u + i + 1u); }
  return key;
}

Uuid MakeUuid(uint8_t n) {
  Uuid uuid{};
  uuid[0] = static_cast<uint8_t>(n + 1u);
  uuid[15] = static_cast<uint8_t>(n * 7u + 3u);
  return uuid;
}

TEST(TransactionsFilter, AZeroedFilterAsksForNothingInParticular) {
  grdb_transactions_filter filter{};
  filter.min_tx_nr = 5;
  grdb_transactions_filter_init(&filter);
  EXPECT_EQ(filter.role, GRDB_ADDRESS_ROLE_NONE);
  EXPECT_EQ(filter.transaction_type, GRDT_TRANSACTION_NONE);
  EXPECT_EQ(filter.min_tx_nr, 0u);
  EXPECT_EQ(grdb_transactions_filter_address(&filter), nullptr);
  EXPECT_EQ(grdb_transactions_filter_coin_community(&filter), nullptr);
  grdb_transactions_filter_init(nullptr); // a no-op, not a crash
  EXPECT_EQ(grdb_transactions_filter_address(nullptr), nullptr);
  EXPECT_EQ(grdb_transactions_filter_coin_community(nullptr), nullptr);
}

TEST(TransactionsFilter, TheSettersWriteWhatTheyAreGiven) {
  grdb_transactions_filter filter{};
  const Key key = MakeKey(3);
  const Uuid uuid = MakeUuid(2);

  ASSERT_EQ(
      grdb_transactions_filter_set_address(&filter, key.data(), GRDB_ADDRESS_ROLE_BALANCE),
      ARNM_SUCCESS
  );
  EXPECT_EQ(filter.role, GRDB_ADDRESS_ROLE_BALANCE);
  ASSERT_NE(grdb_transactions_filter_address(&filter), nullptr);
  EXPECT_EQ(memcmp(grdb_transactions_filter_address(&filter), key.data(), key.size()), 0);

  ASSERT_EQ(
      grdb_transactions_filter_set_transaction_type(&filter, GRDT_TRANSACTION_DEFERRED_TRANSFER),
      ARNM_SUCCESS
  );
  EXPECT_EQ(filter.transaction_type, GRDT_TRANSACTION_DEFERRED_TRANSFER);

  ASSERT_EQ(grdb_transactions_filter_set_coin_community(&filter, uuid.data()), ARNM_SUCCESS);
  ASSERT_NE(grdb_transactions_filter_coin_community(&filter), nullptr);
  EXPECT_EQ(memcmp(grdb_transactions_filter_coin_community(&filter), uuid.data(), uuid.size()), 0);

  ASSERT_EQ(grdb_transactions_filter_set_tx_range(&filter, 10, 20), ARNM_SUCCESS);
  EXPECT_EQ(filter.min_tx_nr, 10u);
  EXPECT_EQ(filter.max_tx_nr, 20u);
  ASSERT_EQ(grdb_transactions_filter_set_date_range(&filter, 100, 200), ARNM_SUCCESS);
  EXPECT_EQ(filter.from_seconds, 100);
  EXPECT_EQ(filter.to_seconds, 200);

  // and each of them takes its part back again
  ASSERT_EQ(
      grdb_transactions_filter_set_address(&filter, nullptr, GRDB_ADDRESS_ROLE_NONE), ARNM_SUCCESS
  );
  EXPECT_EQ(grdb_transactions_filter_address(&filter), nullptr);
  const Key zero{};
  EXPECT_EQ(memcmp(filter.public_key, zero.data(), zero.size()), 0) << "the key is not left behind";
  ASSERT_EQ(grdb_transactions_filter_set_coin_community(&filter, nullptr), ARNM_SUCCESS);
  EXPECT_EQ(grdb_transactions_filter_coin_community(&filter), nullptr);
  ASSERT_EQ(
      grdb_transactions_filter_set_transaction_type(&filter, GRDT_TRANSACTION_NONE), ARNM_SUCCESS
  );
  EXPECT_EQ(filter.transaction_type, GRDT_TRANSACTION_NONE);
}

TEST(TransactionsFilter, WhatTheSettersRefuse) {
  grdb_transactions_filter filter{};
  const Key key = MakeKey(1);
  const Uuid nil{};

  EXPECT_EQ(
      grdb_transactions_filter_set_address(nullptr, key.data(), GRDB_ADDRESS_ROLE_INVOLVED),
      ARNM_ERROR_NULL_POINTER
  );
  EXPECT_EQ(
      grdb_transactions_filter_set_address(&filter, nullptr, GRDB_ADDRESS_ROLE_INVOLVED),
      ARNM_ERROR_NULL_POINTER
  );
  EXPECT_EQ(
      grdb_transactions_filter_set_address(
          &filter, key.data(), static_cast<grdb_address_role>(GRDB_ADDRESS_ROLE_BALANCE + 1)
      ),
      ARNM_ERROR_INVALID_ENUM_TYPE
  );
  EXPECT_EQ(filter.role, GRDB_ADDRESS_ROLE_NONE) << "a refused setter changes nothing";

  EXPECT_EQ(
      grdb_transactions_filter_set_transaction_type(
          &filter, static_cast<grdt_transaction>(GRDT_TRANSACTION_COUNT)
      ),
      ARNM_ERROR_INVALID_ENUM_TYPE
  );
  EXPECT_EQ(
      grdb_transactions_filter_set_transaction_type(nullptr, GRDT_TRANSACTION_TRANSFER),
      ARNM_ERROR_NULL_POINTER
  );
  // the nil uuid names no community, so it is not a way to spell "any"
  EXPECT_EQ(
      grdb_transactions_filter_set_coin_community(&filter, nil.data()), ARNM_ERROR_INVALID_PARAM
  );
  EXPECT_EQ(
      grdb_transactions_filter_set_coin_community(nullptr, nil.data()), ARNM_ERROR_NULL_POINTER
  );
  EXPECT_EQ(grdb_transactions_filter_set_tx_range(nullptr, 1, 2), ARNM_ERROR_NULL_POINTER);
  EXPECT_EQ(grdb_transactions_filter_set_date_range(nullptr, 1, 2), ARNM_ERROR_NULL_POINTER);

  // a span that matches nothing is an answer, not a mistake
  EXPECT_EQ(grdb_transactions_filter_set_tx_range(&filter, 20, 10), ARNM_SUCCESS);
}

TEST(TransactionsFilter, AFilterOnTheHeapIsTheSameFilter) {
  grdb_transactions_filter *filter = grdb_transactions_filter_create();
  ASSERT_NE(filter, nullptr);
  EXPECT_EQ(filter->role, GRDB_ADDRESS_ROLE_NONE);
  EXPECT_EQ(filter->min_tx_nr, 0u);
  EXPECT_EQ(grdb_transactions_filter_address(filter), nullptr);
  const Key key = MakeKey(9);
  ASSERT_EQ(
      grdb_transactions_filter_set_address(filter, key.data(), GRDB_ADDRESS_ROLE_INVOLVED),
      ARNM_SUCCESS
  );
  EXPECT_EQ(memcmp(grdb_transactions_filter_address(filter), key.data(), key.size()), 0);
  grdb_transactions_filter_free(filter);
  grdb_transactions_filter_free(nullptr); // a no-op, not a crash
}

/** A chain of a few transactions, enough to ask something of. */
struct Chain {
  grdb_transactions index{};
  std::vector<grdw_account_balance> balances;
  Chain() {
    EXPECT_EQ(grdb_transactions_init(&index, nullptr, nullptr), ARNM_SUCCESS);
  }
  ~Chain() {
    grdb_transactions_release(&index);
  }
  Chain(const Chain &) = delete;
  Chain &operator=(const Chain &) = delete;
};

TEST(TransactionsFilter, AFilterKeepsNoPointerIntoTheCaller) {
  Chain chain;
  const Key sender = MakeKey(1);
  const Key recipient = MakeKey(2);
  for (uint32_t i = 0; i < 6; ++i) {
    grdr_complete_transaction tx;
    grdr_complete_transaction_init(&tx);
    tx.tx_nr = 1u + i;
    tx.confirmed_at.seconds = 1700000000 + i * 3600;
    tx.transaction_type = GRDT_TRANSACTION_TRANSFER;
    memcpy(tx.transfer.sender_pubkey, sender.data(), SIGN_PUBLIC_KEY_SIZE);
    memcpy(tx.transfer.recipient_pubkey, recipient.data(), SIGN_PUBLIC_KEY_SIZE);
    ASSERT_EQ(grdb_transactions_add(&chain.index, &tx), ARNM_SUCCESS);
  }

  // built through the setters from a key that is gone before the question is asked
  grdb_transactions_filter *asked = grdb_transactions_filter_create();
  ASSERT_NE(asked, nullptr);
  {
    std::vector<uint8_t> temporary(recipient.begin(), recipient.end());
    ASSERT_EQ(
        grdb_transactions_filter_set_address(asked, temporary.data(), GRDB_ADDRESS_ROLE_INVOLVED),
        ARNM_SUCCESS
    );
    std::fill(temporary.begin(), temporary.end(), static_cast<uint8_t>(0xee));
  }

  uint64_t count = 0;
  ASSERT_EQ(grdb_transactions_count(&chain.index, asked, &count), ARNM_SUCCESS);
  EXPECT_EQ(count, 6u);

  // and the same filter written field by field answers the same
  grdb_transactions_filter written{};
  memcpy(written.public_key, recipient.data(), SIGN_PUBLIC_KEY_SIZE);
  written.role = GRDB_ADDRESS_ROLE_INVOLVED;
  uint64_t written_count = 0;
  ASSERT_EQ(grdb_transactions_count(&chain.index, &written, &written_count), ARNM_SUCCESS);
  EXPECT_EQ(written_count, count);

  grdb_transactions_filter_free(asked);
}

} // namespace
