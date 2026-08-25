#include "arnm/arena.h"
#include "arnm/json_reader.h"
#include "arnm/json_writer.h"
#include "arnm/memory.h"
#include "arnm/memory_block.h"
#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/ledger_anchor.h"
#include "gradido_blockchain_core/mapping/json_from_runtime.h"
#include "gradido_blockchain_core/mapping/runtime_from_json.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/types/address.h"
#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_blockchain_core/types/cross_group.h"
#include "gradido_blockchain_core/types/ledger_anchor.h"
#include "gradido_blockchain_core/types/memo_key.h"
#include "gradido_blockchain_core/types/transaction.h"

#include "memory_limit.h"
#include <gtest/gtest.h>
#include <string.h>
#include <string>

/*
 * The two JSON mappings are each other's inverse, so almost every test here is the same shape:
 * build a transaction by hand, write it, read it back, and hold the two side by side. What is
 * being checked is not that the text looks a certain way but that nothing falls out of it on
 * the way there and back -- a field the writer forgets and the reader defaults would pass any
 * check of the document alone.
 *
 * The fixtures are built rather than decoded, so this binary needs no libsodium and covers
 * every branch of both unions, which one sample transaction never would.
 */

namespace {

/** Deterministic filler, so a mismatch names which field moved rather than which run it was. */
void fillBytes(uint8_t *data, uint32_t size, uint8_t seed) {
  for (uint32_t i = 0; i < size; ++i) { data[i] = (uint8_t)(seed + i * 7u); }
}

/** A transaction with its own arena, released whatever the test does. */
class Transaction {
public:
  explicit Transaction(uint32_t arena_size = 0) {
    grdr_complete_transaction_init(&tx_);
    if (arena_size) { EXPECT_EQ(ARNM_SUCCESS, arnm_init_arena(&tx_.memory_area, arena_size)); }
  }
  ~Transaction() {
    grdr_complete_transaction_release(&tx_);
  }
  Transaction(const Transaction &) = delete;
  Transaction &operator=(const Transaction &) = delete;

  grdr_complete_transaction *operator->() {
    return &tx_;
  }
  grdr_complete_transaction &get() {
    return tx_;
  }

private:
  grdr_complete_transaction tx_;
};

/** The envelope every transaction carries, whatever its type. */
void fillEnvelope(grdr_complete_transaction &tx, grdt_transaction type) {
  tx.tx_nr = 4711;
  tx.confirmed_at.seconds = 1750000000;
  tx.confirmed_at.nanos = 123456789;
  tx.created_at.seconds = 1749999000;
  tx.created_at.nanos = 42;
  fillBytes(tx.tx_community_uuid, ARNM_UUID_BINARY_SIZE, 0x10);
  tx.ledger_anchor.type = GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_ID;
  tx.ledger_anchor.id = 987654321;
  tx.transaction_type = type;
  tx.balance_derivation_type = GRDT_BALANCE_DERIVATION_NODE;
  tx.cross_group_type = GRDT_CROSS_GROUP_LOCAL;
  fillBytes(tx.tx_running_hash, GENERIC_HASH_SIZE, 0x20);
}

/** One balance, one memo and one signature pair, all drawn from the transaction's own arena. */
void fillArrays(grdr_complete_transaction &tx) {
  ASSERT_EQ(
      ARNM_SUCCESS,
      arnm_alloc((uint8_t **)&tx.account_balances, sizeof(grdw_account_balance), &tx.memory_area)
  );
  fillBytes(tx.account_balances[0].pubkey, SIGN_PUBLIC_KEY_SIZE, 0x30);
  tx.account_balances[0].balance = -123456;
  fillBytes(tx.account_balances[0].community_uuid, ARNM_UUID_BINARY_SIZE, 0x40);
  tx.account_balances_count = 1;

  ASSERT_EQ(
      ARNM_SUCCESS,
      arnm_alloc((uint8_t **)&tx.encrypted_memos, sizeof(grdw_encrypted_memo), &tx.memory_area)
  );
  tx.encrypted_memos[0].type = GRDT_MEMO_KEY_COMMUNITY_SECRET;
  ASSERT_EQ(
      ARNM_SUCCESS, arnm_memory_block_alloc(&tx.encrypted_memos[0].memo, 37, &tx.memory_area)
  );
  fillBytes(tx.encrypted_memos[0].memo.data, tx.encrypted_memos[0].memo.size, 0x50);
  tx.encrypted_memos_count = 1;

  ASSERT_EQ(
      ARNM_SUCCESS,
      arnm_alloc((uint8_t **)&tx.signature_pairs, sizeof(grdw_signature_pair), &tx.memory_area)
  );
  fillBytes(tx.signature_pairs[0].public_key, SIGN_PUBLIC_KEY_SIZE, 0x60);
  fillBytes(tx.signature_pairs[0].signature, SIGN_SIGNATURE_SIZE, 0x70);
  tx.signature_pairs_count = 1;

  ASSERT_EQ(ARNM_SUCCESS, arnm_memory_block_alloc(&tx.body_bytes, 96, &tx.memory_area));
  fillBytes(tx.body_bytes.data, tx.body_bytes.size, 0x80);
}

void expectSameAnchor(const grdw_ledger_anchor &a, const grdw_ledger_anchor &b) {
  ASSERT_EQ(a.type, b.type);
  if (GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID == a.type) {
    EXPECT_EQ(
        a.hiero_transaction_id.transactionValidStart.seconds,
        b.hiero_transaction_id.transactionValidStart.seconds
    );
    EXPECT_EQ(
        a.hiero_transaction_id.transactionValidStart.nanos,
        b.hiero_transaction_id.transactionValidStart.nanos
    );
    EXPECT_EQ(a.hiero_transaction_id.accountID.shardNum, b.hiero_transaction_id.accountID.shardNum);
    EXPECT_EQ(a.hiero_transaction_id.accountID.realmNum, b.hiero_transaction_id.accountID.realmNum);
    EXPECT_EQ(
        a.hiero_transaction_id.accountID.accountNum, b.hiero_transaction_id.accountID.accountNum
    );
  } else {
    EXPECT_EQ(a.id, b.id);
  }
}

/** Everything the two mappings are supposed to carry, compared field for field. */
void expectSame(const grdr_complete_transaction &a, const grdr_complete_transaction &b) {
  EXPECT_EQ(a.tx_nr, b.tx_nr);
  EXPECT_EQ(a.confirmed_at.seconds, b.confirmed_at.seconds);
  EXPECT_EQ(a.confirmed_at.nanos, b.confirmed_at.nanos);
  EXPECT_EQ(a.created_at.seconds, b.created_at.seconds);
  EXPECT_EQ(a.created_at.nanos, b.created_at.nanos);
  EXPECT_EQ(0, memcmp(a.tx_community_uuid, b.tx_community_uuid, ARNM_UUID_BINARY_SIZE));
  expectSameAnchor(a.ledger_anchor, b.ledger_anchor);

  ASSERT_EQ(a.transaction_type, b.transaction_type);
  EXPECT_EQ(a.balance_derivation_type, b.balance_derivation_type);
  EXPECT_EQ(a.cross_group_type, b.cross_group_type);
  EXPECT_EQ(0, memcmp(a.tx_running_hash, b.tx_running_hash, GENERIC_HASH_SIZE));

  switch (a.transaction_type) {
  case GRDT_TRANSACTION_CREATION:
  case GRDT_TRANSACTION_TRANSFER:
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    EXPECT_EQ(0, memcmp(a.transfer.sender_pubkey, b.transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE));
    EXPECT_EQ(
        0, memcmp(a.transfer.recipient_pubkey, b.transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE)
    );
    EXPECT_EQ(a.transfer.amount, b.transfer.amount);
    EXPECT_EQ(
        0, memcmp(
               a.transfer.coin_community_uuid, b.transfer.coin_community_uuid, ARNM_UUID_BINARY_SIZE
           )
    );
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    EXPECT_EQ(
        0, memcmp(
               a.register_address.user_public_key, b.register_address.user_public_key,
               SIGN_PUBLIC_KEY_SIZE
           )
    );
    EXPECT_EQ(
        0, memcmp(a.register_address.name_hash, b.register_address.name_hash, GENERIC_HASH_SIZE)
    );
    EXPECT_EQ(
        0, memcmp(
               a.register_address.account_public_key, b.register_address.account_public_key,
               SIGN_PUBLIC_KEY_SIZE
           )
    );
    break;
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    EXPECT_EQ(
        0, memcmp(a.community_root.public_key, b.community_root.public_key, SIGN_PUBLIC_KEY_SIZE)
    );
    EXPECT_EQ(
        0,
        memcmp(
            a.community_root.gmw_public_key, b.community_root.gmw_public_key, SIGN_PUBLIC_KEY_SIZE
        )
    );
    EXPECT_EQ(
        0,
        memcmp(
            a.community_root.auf_public_key, b.community_root.auf_public_key, SIGN_PUBLIC_KEY_SIZE
        )
    );
    break;
  default:
    break;
  }

  switch (a.transaction_type) {
  case GRDT_TRANSACTION_CREATION:
    EXPECT_EQ(a.target_date, b.target_date);
    break;
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
    EXPECT_EQ(a.timeout_duration, b.timeout_duration);
    break;
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
  case GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER:
    EXPECT_EQ(a.previous_tx, b.previous_tx);
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    EXPECT_EQ(a.address_type, b.address_type);
    EXPECT_EQ(a.derivation_index, b.derivation_index);
    break;
  default:
    break;
  }

  ASSERT_EQ(a.account_balances_count, b.account_balances_count);
  for (size_t i = 0; i < a.account_balances_count; ++i) {
    EXPECT_EQ(
        0, memcmp(a.account_balances[i].pubkey, b.account_balances[i].pubkey, SIGN_PUBLIC_KEY_SIZE)
    );
    EXPECT_EQ(a.account_balances[i].balance, b.account_balances[i].balance);
    EXPECT_EQ(
        0, memcmp(
               a.account_balances[i].community_uuid, b.account_balances[i].community_uuid,
               ARNM_UUID_BINARY_SIZE
           )
    );
  }

  ASSERT_EQ(a.encrypted_memos_count, b.encrypted_memos_count);
  for (size_t i = 0; i < a.encrypted_memos_count; ++i) {
    EXPECT_EQ(a.encrypted_memos[i].type, b.encrypted_memos[i].type);
    ASSERT_EQ(a.encrypted_memos[i].memo.size, b.encrypted_memos[i].memo.size);
    if (a.encrypted_memos[i].memo.size) {
      EXPECT_EQ(
          0, memcmp(
                 a.encrypted_memos[i].memo.data, b.encrypted_memos[i].memo.data,
                 a.encrypted_memos[i].memo.size
             )
      );
    }
  }

  ASSERT_EQ(a.signature_pairs_count, b.signature_pairs_count);
  for (size_t i = 0; i < a.signature_pairs_count; ++i) {
    EXPECT_EQ(
        0,
        memcmp(
            a.signature_pairs[i].public_key, b.signature_pairs[i].public_key, SIGN_PUBLIC_KEY_SIZE
        )
    );
    EXPECT_EQ(
        0,
        memcmp(a.signature_pairs[i].signature, b.signature_pairs[i].signature, SIGN_SIGNATURE_SIZE)
    );
  }

  ASSERT_EQ(nullptr == a.tx_pairing_community_uuid, nullptr == b.tx_pairing_community_uuid);
  if (a.tx_pairing_community_uuid) {
    EXPECT_EQ(
        0, memcmp(a.tx_pairing_community_uuid, b.tx_pairing_community_uuid, ARNM_UUID_BINARY_SIZE)
    );
  }
  ASSERT_EQ(nullptr == a.pairing_ledger_anchor, nullptr == b.pairing_ledger_anchor);
  if (a.pairing_ledger_anchor) {
    expectSameAnchor(*a.pairing_ledger_anchor, *b.pairing_ledger_anchor);
  }

  ASSERT_EQ(a.body_bytes.size, b.body_bytes.size);
  // an empty block is a NULL pointer on both sides, and memcmp may not be handed one even for
  // no bytes at all
  if (a.body_bytes.size) {
    EXPECT_EQ(0, memcmp(a.body_bytes.data, b.body_bytes.data, a.body_bytes.size));
  }
}

/** Write, read back, compare -- and hand the text out for whoever wants to look at it too. */
std::string roundTrip(grdr_complete_transaction &source, arnm_json_write_flags flags = 0) {
  // generous on purpose: it has to hold the document being built, the rendered text and the
  // parse of it again, for the widest fixture any test here builds
  arnm scratch{};
  EXPECT_EQ(ARNM_SUCCESS, arnm_init_arena(&scratch, 1024 * 1024));

  arnm_memory_block text{};
  const arnm_result written = grdm_json_from_complete_transaction(&text, &source, &scratch, flags);
  EXPECT_TRUE(ARNM_SUCCESS == written || ARNM_WARNING_ARENA_MEMORY_NOT_RECLAIMED == written)
      << grd_result_to_string(written);

  std::string json((const char *)text.data, text.size - 1);

  Transaction target;
  const arnm_result read = grdm_complete_transaction_from_json(
      &target.get(), json.c_str(), (uint32_t)json.size(), &scratch, ARNM_JSON_READ_DEFAULT
  );
  EXPECT_EQ(ARNM_SUCCESS, read) << grd_result_to_string(read);
  if (ARNM_SUCCESS == read) { expectSame(source, target.get()); }

  arnm_release(&scratch);
  return json;
}

} // namespace

TEST(JsonMappingTest, Transfer_RoundTrip) {
  Transaction source(4096);
  fillEnvelope(source.get(), GRDT_TRANSACTION_TRANSFER);
  fillBytes(source->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x01);
  fillBytes(source->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x02);
  source->transfer.amount = 1234567890;
  fillBytes(source->transfer.coin_community_uuid, ARNM_UUID_BINARY_SIZE, 0x03);
  fillArrays(source.get());

  const std::string json = roundTrip(source.get());
  EXPECT_NE(std::string::npos, json.find("\"transaction_type\":\"GRDT_TRANSACTION_TRANSFER\""));
  EXPECT_NE(std::string::npos, json.find("\"amount\":1234567890"));
  // binary travels as lowercase hex, uuids in the canonical dashed form
  EXPECT_NE(
      std::string::npos, json.find("\"tx_community_uuid\":\"10171e25-2c33-3a41-484f-565d646b7279\"")
  );
}

TEST(JsonMappingTest, Creation_RoundTrip_WithHieroAnchor) {
  Transaction source(4096);
  fillEnvelope(source.get(), GRDT_TRANSACTION_CREATION);
  // a creation carries the run of zeros the wire mapping leaves in the sender key
  memset(source->transfer.sender_pubkey, 0, SIGN_PUBLIC_KEY_SIZE);
  fillBytes(source->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x04);
  source->transfer.amount = 10000000;
  fillBytes(source->transfer.coin_community_uuid, ARNM_UUID_BINARY_SIZE, 0x05);
  source->target_date = 1751000000;

  source->ledger_anchor.type = GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID;
  source->ledger_anchor.hiero_transaction_id.transactionValidStart.seconds = 1750000001;
  source->ledger_anchor.hiero_transaction_id.transactionValidStart.nanos = 7;
  source->ledger_anchor.hiero_transaction_id.accountID.shardNum = 1;
  source->ledger_anchor.hiero_transaction_id.accountID.realmNum = 2;
  source->ledger_anchor.hiero_transaction_id.accountID.accountNum = 3;
  fillArrays(source.get());

  const std::string json = roundTrip(source.get());
  EXPECT_NE(std::string::npos, json.find("\"target_date\":1751000000"));
  EXPECT_NE(std::string::npos, json.find("\"account_num\":3"));
}

TEST(JsonMappingTest, RegisterAddress_RoundTrip) {
  Transaction source(4096);
  fillEnvelope(source.get(), GRDT_TRANSACTION_REGISTER_ADDRESS);
  fillBytes(source->register_address.user_public_key, SIGN_PUBLIC_KEY_SIZE, 0x06);
  fillBytes(source->register_address.name_hash, GENERIC_HASH_SIZE, 0x07);
  fillBytes(source->register_address.account_public_key, SIGN_PUBLIC_KEY_SIZE, 0x08);
  source->address_type = GRDT_ADDRESS_COMMUNITY_HUMAN;
  source->derivation_index = 17;
  fillArrays(source.get());

  const std::string json = roundTrip(source.get());
  EXPECT_NE(std::string::npos, json.find("\"address_type\":\"GRDT_ADDRESS_COMMUNITY_HUMAN\""));
  EXPECT_NE(std::string::npos, json.find("\"derivation_index\":17"));
}

TEST(JsonMappingTest, CommunityRoot_RoundTrip) {
  Transaction source(4096);
  fillEnvelope(source.get(), GRDT_TRANSACTION_COMMUNITY_ROOT);
  fillBytes(source->community_root.public_key, SIGN_PUBLIC_KEY_SIZE, 0x09);
  fillBytes(source->community_root.gmw_public_key, SIGN_PUBLIC_KEY_SIZE, 0x0a);
  fillBytes(source->community_root.auf_public_key, SIGN_PUBLIC_KEY_SIZE, 0x0b);
  source->balance_derivation_type = GRDT_BALANCE_DERIVATION_EXTERN;
  fillArrays(source.get());

  roundTrip(source.get());
}

TEST(JsonMappingTest, DeferredTransfer_RoundTrip) {
  Transaction source(4096);
  fillEnvelope(source.get(), GRDT_TRANSACTION_DEFERRED_TRANSFER);
  fillBytes(source->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x0c);
  fillBytes(source->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x0d);
  source->transfer.amount = 5000;
  fillBytes(source->transfer.coin_community_uuid, ARNM_UUID_BINARY_SIZE, 0x0e);
  source->timeout_duration = 86400;
  fillArrays(source.get());

  const std::string json = roundTrip(source.get());
  EXPECT_NE(std::string::npos, json.find("\"timeout_duration\":86400"));
}

TEST(JsonMappingTest, RedeemDeferredTransfer_RoundTrip) {
  Transaction source(4096);
  fillEnvelope(source.get(), GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER);
  fillBytes(source->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x0f);
  fillBytes(source->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x11);
  source->transfer.amount = 250000;
  fillBytes(source->transfer.coin_community_uuid, ARNM_UUID_BINARY_SIZE, 0x12);
  source->previous_tx = 4242;
  fillArrays(source.get());

  const std::string json = roundTrip(source.get());
  EXPECT_NE(std::string::npos, json.find("\"previous_tx\":4242"));
}

TEST(JsonMappingTest, TimeoutDeferredTransfer_RoundTrip) {
  Transaction source(4096);
  fillEnvelope(source.get(), GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER);
  source->previous_tx = 99;
  fillArrays(source.get());

  const std::string json = roundTrip(source.get());
  // this type owns no detail object at all -- the wire mapping copies nothing but the link back
  EXPECT_EQ(std::string::npos, json.find("\"transfer\""));
  EXPECT_NE(std::string::npos, json.find("\"previous_tx\":99"));
}

TEST(JsonMappingTest, CrossGroup_PairingMembers_RoundTrip) {
  Transaction source(4096);
  fillEnvelope(source.get(), GRDT_TRANSACTION_TRANSFER);
  fillBytes(source->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x13);
  fillBytes(source->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x14);
  source->transfer.amount = 1;
  fillBytes(source->transfer.coin_community_uuid, ARNM_UUID_BINARY_SIZE, 0x15);
  fillArrays(source.get());

  source->cross_group_type = GRDT_CROSS_GROUP_OUTBOUND;
  ASSERT_EQ(
      ARNM_SUCCESS,
      arnm_alloc(&source->tx_pairing_community_uuid, ARNM_UUID_BINARY_SIZE, &source->memory_area)
  );
  fillBytes(source->tx_pairing_community_uuid, ARNM_UUID_BINARY_SIZE, 0x16);
  ASSERT_EQ(
      ARNM_SUCCESS, arnm_alloc(
                        (uint8_t **)&source->pairing_ledger_anchor, sizeof(grdw_ledger_anchor),
                        &source->memory_area
                    )
  );
  source->pairing_ledger_anchor->type = GRDT_LEDGER_ANCHOR_NODE_TRIGGER_TRANSACTION_ID;
  source->pairing_ledger_anchor->id = 77;

  const std::string json = roundTrip(source.get());
  EXPECT_NE(std::string::npos, json.find("\"cross_group_type\":\"GRDT_CROSS_GROUP_OUTBOUND\""));
  EXPECT_NE(std::string::npos, json.find("\"pairing_ledger_anchor\""));
}

TEST(JsonMappingTest, EmptyArrays_And_EmptyBodyBytes_RoundTrip) {
  Transaction source;
  fillEnvelope(source.get(), GRDT_TRANSACTION_TRANSFER);
  fillBytes(source->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x17);
  fillBytes(source->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x18);
  source->transfer.amount = 0;
  fillBytes(source->transfer.coin_community_uuid, ARNM_UUID_BINARY_SIZE, 0x19);

  const std::string json = roundTrip(source.get());
  EXPECT_NE(std::string::npos, json.find("\"account_balances\":[]"));
  EXPECT_NE(std::string::npos, json.find("\"body_bytes\":\"\""));
  // a local transaction carries neither pairing member, so neither is written
  EXPECT_EQ(std::string::npos, json.find("tx_pairing_community_uuid"));
}

TEST(JsonMappingTest, Pretty_ReadsBackTheSame) {
  Transaction source(4096);
  fillEnvelope(source.get(), GRDT_TRANSACTION_TRANSFER);
  fillBytes(source->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x1a);
  fillBytes(source->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x1b);
  source->transfer.amount = -1;
  fillBytes(source->transfer.coin_community_uuid, ARNM_UUID_BINARY_SIZE, 0x1c);
  fillArrays(source.get());

  const std::string json = roundTrip(source.get(), ARNM_JSON_WRITE_PRETTY);
  EXPECT_NE(std::string::npos, json.find('\n'));
}

TEST(JsonMappingTest, ManyElements_EveryOneInItsOwnPlace) {
  // The single-element arrays above would let a walk that never advances, or one that reads the
  // same element every time, pass unnoticed. Here every element carries a different seed, so a
  // walk that slips by one or stalls shows up as a mismatch at the element it slipped on.
  constexpr uint32_t kBalances = 64;
  constexpr uint32_t kMemos = 8;
  constexpr uint32_t kSignatures = 32;

  Transaction source(256 * 1024);
  fillEnvelope(source.get(), GRDT_TRANSACTION_TRANSFER);
  fillBytes(source->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x24);
  fillBytes(source->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x25);
  source->transfer.amount = 42;
  fillBytes(source->transfer.coin_community_uuid, ARNM_UUID_BINARY_SIZE, 0x26);

  ASSERT_EQ(
      ARNM_SUCCESS, arnm_alloc(
                        (uint8_t **)&source->account_balances,
                        kBalances * sizeof(grdw_account_balance), &source->memory_area
                    )
  );
  for (uint32_t i = 0; i < kBalances; ++i) {
    fillBytes(source->account_balances[i].pubkey, SIGN_PUBLIC_KEY_SIZE, (uint8_t)(0x80 + i));
    source->account_balances[i].balance = -1000 - (int64_t)i;
    fillBytes(
        source->account_balances[i].community_uuid, ARNM_UUID_BINARY_SIZE, (uint8_t)(0xc0 + i)
    );
  }
  source->account_balances_count = kBalances;

  ASSERT_EQ(
      ARNM_SUCCESS, arnm_alloc(
                        (uint8_t **)&source->encrypted_memos, kMemos * sizeof(grdw_encrypted_memo),
                        &source->memory_area
                    )
  );
  for (uint32_t i = 0; i < kMemos; ++i) {
    // memos of different lengths as well as different content: the sizing pass adds each one up
    // on its own, and a walk that mispairs a length with an element would open too small an arena
    source->encrypted_memos[i].type =
        (0 == i % 2) ? GRDT_MEMO_KEY_PLAIN : GRDT_MEMO_KEY_COMMUNITY_SECRET;
    ASSERT_EQ(
        ARNM_SUCCESS,
        arnm_memory_block_alloc(&source->encrypted_memos[i].memo, 8 + i * 13, &source->memory_area)
    );
    fillBytes(
        source->encrypted_memos[i].memo.data, source->encrypted_memos[i].memo.size,
        (uint8_t)(0x11 + i)
    );
  }
  source->encrypted_memos_count = kMemos;

  ASSERT_EQ(
      ARNM_SUCCESS, arnm_alloc(
                        (uint8_t **)&source->signature_pairs,
                        kSignatures * sizeof(grdw_signature_pair), &source->memory_area
                    )
  );
  for (uint32_t i = 0; i < kSignatures; ++i) {
    fillBytes(source->signature_pairs[i].public_key, SIGN_PUBLIC_KEY_SIZE, (uint8_t)(0x33 + i));
    fillBytes(source->signature_pairs[i].signature, SIGN_SIGNATURE_SIZE, (uint8_t)(0x55 + i));
  }
  source->signature_pairs_count = kSignatures;

  ASSERT_EQ(ARNM_SUCCESS, arnm_memory_block_alloc(&source->body_bytes, 200, &source->memory_area));
  fillBytes(source->body_bytes.data, source->body_bytes.size, 0x99);

  roundTrip(source.get());
}

TEST(JsonMappingTest, HostAllocator_LeavesNothingBehind) {
  // the arena paths above give everything back in one stroke; with the host there is no such
  // stroke, so this is the run that shows the writer and the reader hand back what they took.
  // Under ASan's leak check it is the whole point of the test.
  Transaction source(4096);
  fillEnvelope(source.get(), GRDT_TRANSACTION_TRANSFER);
  fillBytes(source->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x21);
  fillBytes(source->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x22);
  source->transfer.amount = 999;
  fillBytes(source->transfer.coin_community_uuid, ARNM_UUID_BINARY_SIZE, 0x23);
  fillArrays(source.get());

  arnm_memory_block text{};
  ASSERT_EQ(ARNM_SUCCESS, grdm_json_from_complete_transaction(&text, &source.get(), nullptr, 0));
  ASSERT_NE(nullptr, text.data);

  Transaction target;
  ASSERT_EQ(
      ARNM_SUCCESS,
      grdm_complete_transaction_from_json(
          &target.get(), (const char *)text.data, text.size - 1, nullptr, ARNM_JSON_READ_DEFAULT
      )
  );
  expectSame(source.get(), target.get());
  EXPECT_EQ(ARNM_SUCCESS, arnm_memory_block_free(&text, nullptr));
}

TEST(JsonMappingTest, ReusedTarget_IsReleasedNotLeaked) {
  Transaction source(4096);
  fillEnvelope(source.get(), GRDT_TRANSACTION_TRANSFER);
  fillBytes(source->transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x1d);
  fillBytes(source->transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x1e);
  source->transfer.amount = 7;
  fillBytes(source->transfer.coin_community_uuid, ARNM_UUID_BINARY_SIZE, 0x1f);
  fillArrays(source.get());

  arnm scratch{};
  ASSERT_EQ(ARNM_SUCCESS, arnm_init_arena(&scratch, 64 * 1024));
  arnm_memory_block text{};
  const arnm_result written =
      grdm_json_from_complete_transaction(&text, &source.get(), &scratch, 0);
  ASSERT_TRUE(ARNM_SUCCESS == written || ARNM_WARNING_ARENA_MEMORY_NOT_RECLAIMED == written);
  const std::string json((const char *)text.data, text.size - 1);

  Transaction target;
  for (int round = 0; round < 3; ++round) {
    ASSERT_EQ(
        ARNM_SUCCESS,
        grdm_complete_transaction_from_json(
            &target.get(), json.c_str(), (uint32_t)json.size(), &scratch, ARNM_JSON_READ_DEFAULT
        )
    );
    expectSame(source.get(), target.get());
  }
  arnm_release(&scratch);
}

// ********** what a document is refused for ************************************************

namespace {

/** Read a document and hand back only the verdict; the transaction is released either way. */
arnm_result readVerdict(const std::string &json) {
  Transaction target;
  return grdm_complete_transaction_from_json(
      &target.get(), json.c_str(), (uint32_t)json.size(), nullptr, ARNM_JSON_READ_DEFAULT
  );
}

/** A minimal transfer document, with @p replace_from swapped for @p replace_to. */
std::string transferDocument(
    const std::string &replace_from = "", const std::string &replace_to = ""
) {
  std::string json = "{\"tx_nr\":1,"
                     "\"confirmed_at\":{\"seconds\":2,\"nanos\":3},"
                     "\"created_at\":{\"seconds\":4,\"nanos\":5},"
                     "\"tx_community_uuid\":\"019e2c31-a303-75c0-941e-f35c59e4f978\","
                     "\"ledger_anchor\":{\"type\":\"GRDT_LEDGER_ANCHOR_UNSPECIFIED\"},"
                     "\"transaction_type\":\"GRDT_TRANSACTION_TRANSFER\","
                     "\"balance_derivation_type\":\"GRDT_BALANCE_DERIVATION_NODE\","
                     "\"cross_group_type\":\"GRDT_CROSS_GROUP_LOCAL\","
                     "\"tx_running_hash\":\"" +
                     std::string(GENERIC_HASH_SIZE * 2, 'a') +
                     "\","
                     "\"transfer\":{\"sender_pubkey\":\"" +
                     std::string(SIGN_PUBLIC_KEY_SIZE * 2, 'b') + "\",\"recipient_pubkey\":\"" +
                     std::string(SIGN_PUBLIC_KEY_SIZE * 2, 'c') +
                     "\",\"amount\":6,"
                     "\"coin_community_uuid\":\"019e2c31-a303-75c0-941e-f35c59e4f978\"},"
                     "\"account_balances\":[],\"encrypted_memos\":[],\"signature_pairs\":[],"
                     "\"body_bytes\":\"\"}";
  if (!replace_from.empty()) {
    const size_t at = json.find(replace_from);
    EXPECT_NE(std::string::npos, at) << replace_from;
    if (std::string::npos != at) { json.replace(at, replace_from.size(), replace_to); }
  }
  return json;
}

} // namespace

TEST(JsonMappingTest, Refuses_NotJson) {
  EXPECT_EQ(ARNM_ERROR_DECODE_FAILED, readVerdict("not a document at all"));
}

TEST(JsonMappingTest, Accepts_TheMinimalDocument) {
  EXPECT_EQ(ARNM_SUCCESS, readVerdict(transferDocument()));
}

TEST(JsonMappingTest, Refuses_HexOfTheWrongLength) {
  // one byte short of a public key: the string is there and is hex, and only its length is wrong
  const std::string too_short =
      "\"sender_pubkey\":\"" + std::string(SIGN_PUBLIC_KEY_SIZE * 2 - 2, 'b');
  const std::string original = "\"sender_pubkey\":\"" + std::string(SIGN_PUBLIC_KEY_SIZE * 2, 'b');
  EXPECT_EQ(ARNM_ERROR_DECODE_FAILED, readVerdict(transferDocument(original, too_short)));
}

TEST(JsonMappingTest, Refuses_HexThatIsNotHex) {
  const std::string original(SIGN_PUBLIC_KEY_SIZE * 2, 'c');
  std::string broken = original;
  broken[3] = 'z';
  EXPECT_EQ(ARNM_ERROR_DECODE_FAILED, readVerdict(transferDocument(original, broken)));
}

TEST(JsonMappingTest, Refuses_UuidThatIsNotOne) {
  EXPECT_EQ(
      ARNM_ERROR_DECODE_FAILED,
      readVerdict(transferDocument("019e2c31-a303-75c0-941e-f35c59e4f978", "not-a-uuid"))
  );
}

TEST(JsonMappingTest, Refuses_UnknownEnumeratorName) {
  EXPECT_EQ(
      ARNM_ERROR_ENUM_UNKNOWN,
      readVerdict(transferDocument("GRDT_TRANSACTION_TRANSFER", "GRDT_TRANSACTION_SOMETHING"))
  );
}

TEST(JsonMappingTest, Refuses_TransactionTypeWithoutALayout) {
  EXPECT_EQ(
      ARNM_ERROR_ENUM_UNHANDLED,
      readVerdict(
          transferDocument("GRDT_TRANSACTION_TRANSFER", "GRDT_TRANSACTION_COMMUNITY_FRIENDS_UPDATE")
      )
  );
}

TEST(JsonMappingTest, Refuses_MissingMember) {
  EXPECT_NE(ARNM_SUCCESS, readVerdict(transferDocument("\"tx_running_hash\"", "\"other_name\"")));
}

TEST(JsonMappingTest, Refuses_MemberOfTheWrongType) {
  EXPECT_NE(ARNM_SUCCESS, readVerdict(transferDocument("\"amount\":6", "\"amount\":\"six\"")));
}

TEST(JsonMappingTest, Accepts_OptionalMembersWrittenAsNull) {
  // arnm counts a null member and an absent one as the same thing, and the reader keeps that:
  // the arrays and the two pairing members are optional, so a document that spells them out as
  // null says the same as one that leaves them out.
  EXPECT_EQ(
      ARNM_SUCCESS,
      readVerdict(transferDocument("\"account_balances\":[]", "\"account_balances\":null"))
  );
  EXPECT_EQ(
      ARNM_SUCCESS,
      readVerdict(transferDocument("\"signature_pairs\":[]", "\"signature_pairs\":null"))
  );
}

TEST(JsonMappingTest, Refuses_ArrayThatIsNoArray) {
  // an optional member is allowed to be missing, not to be something else
  EXPECT_EQ(
      ARNM_ERROR_INVALID_ENUM_TYPE,
      readVerdict(transferDocument("\"account_balances\":[]", "\"account_balances\":7"))
  );
}

TEST(JsonMappingTest, ReadsMembersInWhateverOrderTheDocumentPutThem) {
  // A JSON object promises no order, and this mapping needs transaction_type before it can know
  // which detail member matters. The walk collects first and decides afterwards, so a document
  // that puts the type last has to read exactly like one that puts it first.
  std::string reordered = transferDocument();
  const std::string type_member = ",\"transaction_type\":\"GRDT_TRANSACTION_TRANSFER\"";
  const size_t at = reordered.find(type_member.substr(1));
  ASSERT_NE(std::string::npos, at);
  reordered.erase(at - 1, type_member.size());
  reordered.insert(reordered.size() - 1, type_member);
  EXPECT_EQ(ARNM_SUCCESS, readVerdict(reordered)) << reordered;
}

TEST(JsonMappingTest, Refuses_NullArgumentsAndEmptyInput) {
  Transaction target;
  EXPECT_EQ(
      ARNM_ERROR_NULL_POINTER,
      grdm_complete_transaction_from_json(nullptr, "{}", 2, nullptr, ARNM_JSON_READ_DEFAULT)
  );
  EXPECT_EQ(
      ARNM_ERROR_NULL_POINTER, grdm_complete_transaction_from_json(
                                   &target.get(), nullptr, 2, nullptr, ARNM_JSON_READ_DEFAULT
                               )
  );
  EXPECT_EQ(
      ARNM_ERROR_INVALID_PARAM,
      grdm_complete_transaction_from_json(&target.get(), "{}", 0, nullptr, ARNM_JSON_READ_DEFAULT)
  );

  arnm_memory_block text{};
  EXPECT_EQ(
      ARNM_ERROR_NULL_POINTER,
      grdm_json_from_complete_transaction(nullptr, &target.get(), nullptr, 0)
  );
  EXPECT_EQ(
      ARNM_ERROR_NULL_POINTER, grdm_json_from_complete_transaction(&text, nullptr, nullptr, 0)
  );
}

TEST(JsonMappingTest, Refuses_WritingATransactionTypeWithoutALayout) {
  Transaction source;
  fillEnvelope(source.get(), GRDT_TRANSACTION_COMMUNITY_FRIENDS_UPDATE);
  arnm_memory_block text{};
  EXPECT_EQ(
      ARNM_ERROR_ENUM_UNHANDLED,
      grdm_json_from_complete_transaction(&text, &source.get(), nullptr, 0)
  );
  // a refusal leaves the output exactly as the caller had it
  EXPECT_EQ(nullptr, text.data);
  EXPECT_EQ(0u, text.size);
}
