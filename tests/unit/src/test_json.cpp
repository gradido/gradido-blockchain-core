#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/confirmed_transaction.h"
#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/data/wire/ledger_anchor.h"
#include "gradido_blockchain_core/data/wire/specific_transactions.h"
#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "gradido_blockchain_core/mapping/json_from_runtime.h"
#include "gradido_blockchain_core/mapping/json_from_wire.h"
#include "gradido_blockchain_core/mapping/runtime_from_wire.h"
#include "hostmem/memory.h"
#include "hostmem/multi_arena.h"

#include "memory_limit.h"
#include "yyjson.h"
#include <functional>
#include <gtest/gtest.h>
#include <string.h>
#include <string>

// The transactions below are assembled field by field rather than decoded from a fixture: the
// point here is what the mapping writes, and a hand built struct is the only way to put a value
// in a field and then look for exactly that value in the text. Decoding is test_runtime's part.

namespace {

/**
 * @brief The two chains the mapping asks for, opened together and released together.
 *
 * Small arenas on purpose -- 4 KiB apiece, well under what a transaction of this size needs in
 * one stretch -- so every test here also walks the chain across an arena boundary. A capacity
 * that swallowed the whole run would never exercise that.
 */
struct Chains {
  hostmem_multi_arena work{};
  hostmem_multi_arena result{};

  Chains() {
    EXPECT_EQ(hostmem_multi_arena_init(&work, 4096, 0, nullptr), HOSTMEM_SUCCESS);
    EXPECT_EQ(hostmem_multi_arena_init(&result, 4096, 0, nullptr), HOSTMEM_SUCCESS);
  }
  ~Chains() {
    hostmem_multi_arena_release(&work);
    hostmem_multi_arena_release(&result);
  }
};

//! Fill a byte array with a recognizable ramp, so a misplaced field shows up as wrong hex.
void ramp(uint8_t *data, size_t size, uint8_t first) {
  for (size_t i = 0; i < size; ++i) { data[i] = (uint8_t)(first + i); }
}

const uint8_t kCommunityUuid[HOSTMEM_UUID_BINARY_SIZE] = {0x01, 0x9e, 0x2c, 0x31, 0xa3, 0x03,
                                                          0x75, 0xc0, 0x94, 0x1e, 0xf3, 0x5c,
                                                          0x59, 0xe4, 0xf9, 0x78};

/**
 * @brief A transfer with one balance, one memo and one signature.
 *
 * The arrays point at storage the caller owns, not at an arena of the transaction's own, so
 * grdr_complete_transaction_release() must not be called on it -- the zeroed memory_area would
 * be released, which is harmless, but the pointers are not the arena's to free. The tests below
 * simply let it go out of scope.
 */
grdr_complete_transaction makeTransfer(
    grdw_account_balance *balance, grdw_encrypted_memo *memo, grdw_signature_pair *signature
) {
  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);

  tx.tx_nr = 121;
  tx.created_at = {.seconds = 1700000000, .nanos = 2912};
  tx.confirmed_at = {.seconds = 1700000060, .nanos = 0};
  memcpy(tx.tx_community_uuid, kCommunityUuid, HOSTMEM_UUID_BINARY_SIZE);
  tx.ledger_anchor.type = GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_ID;
  tx.ledger_anchor.id = 4711;

  tx.transaction_type = GRDT_TRANSACTION_TRANSFER;
  tx.balance_derivation_type = GRDT_BALANCE_DERIVATION_NODE;
  tx.cross_group_type = GRDT_CROSS_GROUP_LOCAL;

  ramp(tx.transfer.sender_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x10);
  ramp(tx.transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x40);
  tx.transfer.amount = 12345; // 1.2345 GDD at scale 10^4
  memcpy(tx.transfer.coin_community_uuid, kCommunityUuid, HOSTMEM_UUID_BINARY_SIZE);
  ramp(tx.tx_running_hash, GENERIC_HASH_SIZE, 0x80);

  tx.account_balances = balance;
  tx.account_balances_count = 1;
  tx.encrypted_memos = memo;
  tx.encrypted_memos_count = 1;
  tx.signature_pairs = signature;
  tx.signature_pairs_count = 1;
  return tx;
}

//! Parse what the mapping wrote. A doc that does not parse is the loudest failure this has.
yyjson_doc *parse(const hostmem_memory_block &json) {
  yyjson_read_err err{};
  yyjson_doc *doc = yyjson_read_opts((char *)json.data, json.size, 0, nullptr, &err);
  EXPECT_TRUE(doc != nullptr) << "yyjson: " << err.msg << " at " << err.pos;
  return doc;
}

const char *getStr(yyjson_val *obj, const char *key) {
  return yyjson_get_str(yyjson_obj_get(obj, key));
}

} // namespace

TEST(JsonFromRuntimeTest, TransferRendersEveryField) {
  Chains chains;

  grdw_account_balance balance{};
  ramp(balance.pubkey, SIGN_PUBLIC_KEY_SIZE, 0x40);
  balance.balance = 987650000; // 98765.0000 GDD
  memcpy(balance.community_uuid, kCommunityUuid, HOSTMEM_UUID_BINARY_SIZE);

  uint8_t memoBytes[4] = {0xde, 0xad, 0xbe, 0xef};
  grdw_encrypted_memo memo{};
  memo.type = GRDT_MEMO_KEY_SHARED_SECRET;
  memo.memo = {memoBytes, sizeof(memoBytes)};

  grdw_signature_pair signature{};
  ramp(signature.public_key, SIGN_PUBLIC_KEY_SIZE, 0x10);
  ramp(signature.signature, SIGN_SIGNATURE_SIZE, 0x01);

  grdr_complete_transaction tx = makeTransfer(&balance, &memo, &signature);

  hostmem_memory_block json{};
  ASSERT_EQ(
      grdm_complete_transaction_to_json(
          &json, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_SUCCESS
  );
  ASSERT_TRUE(json.data != nullptr);
  ASSERT_GT(json.size, 0u);
  // size counts the characters, and the terminator sits after them
  EXPECT_EQ(strlen((const char *)json.data), json.size);

  yyjson_doc *doc = parse(json);
  ASSERT_TRUE(doc != nullptr);
  yyjson_val *root = yyjson_doc_get_root(doc);

  EXPECT_EQ(yyjson_get_uint(yyjson_obj_get(root, "tx_nr")), 121u);
  EXPECT_STREQ(getStr(root, "transaction_type"), "GRDT_TRANSACTION_TRANSFER");
  EXPECT_STREQ(getStr(root, "cross_group_type"), "GRDT_CROSS_GROUP_LOCAL");
  EXPECT_STREQ(getStr(root, "balance_derivation_type"), "GRDT_BALANCE_DERIVATION_NODE");
  EXPECT_STREQ(getStr(root, "created_at"), "1700000000.000002912");
  EXPECT_STREQ(getStr(root, "confirmed_at"), "1700000060.000000000");
  EXPECT_STREQ(getStr(root, "tx_community_uuid"), "019e2c31-a303-75c0-941e-f35c59e4f978");
  EXPECT_STREQ(
      getStr(root, "tx_running_hash"),
      "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f"
  );

  yyjson_val *anchor = yyjson_obj_get(root, "ledger_anchor");
  ASSERT_TRUE(anchor != nullptr);
  EXPECT_STREQ(getStr(anchor, "type"), "GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_ID");
  EXPECT_EQ(yyjson_get_uint(yyjson_obj_get(anchor, "id")), 4711u);

  yyjson_val *transfer = yyjson_obj_get(root, "transfer");
  ASSERT_TRUE(transfer != nullptr);
  EXPECT_STREQ(
      getStr(transfer, "sender_pubkey"),
      "101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f"
  );
  EXPECT_STREQ(
      getStr(transfer, "recipient_pubkey"),
      "404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f"
  );
  // fixed point, four digits, as a string -- never a JSON number
  EXPECT_STREQ(getStr(transfer, "amount"), "1.2345");
  EXPECT_STREQ(getStr(transfer, "coin_community_uuid"), "019e2c31-a303-75c0-941e-f35c59e4f978");

  yyjson_val *balances = yyjson_obj_get(root, "account_balances");
  ASSERT_EQ(yyjson_arr_size(balances), 1u);
  yyjson_val *firstBalance = yyjson_arr_get(balances, 0);
  EXPECT_STREQ(getStr(firstBalance, "balance"), "98765.0000");
  EXPECT_STREQ(getStr(firstBalance, "community_uuid"), "019e2c31-a303-75c0-941e-f35c59e4f978");

  yyjson_val *memos = yyjson_obj_get(root, "encrypted_memos");
  ASSERT_EQ(yyjson_arr_size(memos), 1u);
  EXPECT_STREQ(getStr(yyjson_arr_get(memos, 0), "type"), "GRDT_MEMO_KEY_SHARED_SECRET");
  EXPECT_STREQ(getStr(yyjson_arr_get(memos, 0), "memo"), "deadbeef");

  yyjson_val *signatures = yyjson_obj_get(root, "signature_pairs");
  ASSERT_EQ(yyjson_arr_size(signatures), 1u);
  EXPECT_STREQ(
      getStr(yyjson_arr_get(signatures, 0), "signature"),
      "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"
      "2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40"
  );

  // local transaction: the cross group members are absent rather than null
  EXPECT_TRUE(yyjson_obj_get(root, "tx_pairing_community_uuid") == nullptr);
  EXPECT_TRUE(yyjson_obj_get(root, "pairing_ledger_anchor") == nullptr);
  // no body bytes were set, so no member for them
  EXPECT_TRUE(yyjson_obj_get(root, "body_bytes") == nullptr);

  yyjson_doc_free(doc);
}

/**
 * @brief The text carries its terminator, and it is inside the block the result chain handed out.
 *
 * A fresh arena holds whatever the previous tenant left, which on a quiet run is zeros -- and
 * zeros would let a missing terminator pass unnoticed. So the result chain is filled with a
 * byte that is not one and handed back before the render, and the check then has something to
 * bite on.
 */
TEST(JsonFromRuntimeTest, TextIsTerminatedInsideTheResultChain) {
  Chains chains;

  uint8_t *dirty = nullptr;
  ASSERT_EQ(hostmem_multi_arena_alloc(&dirty, 4096, &chains.result), HOSTMEM_SUCCESS);
  memset(dirty, 0xff, 4096);
  hostmem_multi_arena_reset(&chains.result);

  grdw_account_balance balance{};
  grdw_encrypted_memo memo{};
  grdw_signature_pair signature{};
  grdr_complete_transaction tx = makeTransfer(&balance, &memo, &signature);

  hostmem_memory_block json{};
  ASSERT_EQ(
      grdm_complete_transaction_to_json(
          &json, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_SUCCESS
  );
  ASSERT_GT(json.size, 0u);
  ASSERT_LT(json.size, 4096u) << "the dirty stretch has to reach past the text";
  // size counts the characters; the terminator sits after them, within the same allocation
  EXPECT_EQ(json.data[json.size], '\0');
  EXPECT_EQ(strlen((const char *)json.data), json.size);
}

TEST(JsonFromRuntimeTest, CreationWritesNullSenderAndTargetDate) {
  Chains chains;

  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);
  tx.tx_nr = 1;
  tx.transaction_type = GRDT_TRANSACTION_CREATION;
  ramp(tx.transfer.recipient_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x40);
  tx.transfer.amount = 10000000; // 1000.0000 GDD
  tx.target_date = 1700000000;

  hostmem_memory_block json{};
  ASSERT_EQ(
      grdm_complete_transaction_to_json(
          &json, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_SUCCESS
  );

  yyjson_doc *doc = parse(json);
  ASSERT_TRUE(doc != nullptr);
  yyjson_val *root = yyjson_doc_get_root(doc);

  yyjson_val *transfer = yyjson_obj_get(root, "transfer");
  ASSERT_TRUE(transfer != nullptr);
  // the struct documents those bytes as the absence of a sender, and the text says so too
  EXPECT_TRUE(yyjson_is_null(yyjson_obj_get(transfer, "sender_pubkey")));
  EXPECT_STREQ(getStr(transfer, "amount"), "1000.0000");
  // and a creation names no coin community of its own
  EXPECT_TRUE(yyjson_obj_get(transfer, "coin_community_uuid") == nullptr);

  EXPECT_EQ(yyjson_get_sint(yyjson_obj_get(root, "target_date")), 1700000000);
  // the three arrays are written even when empty, so a reader always finds a list to walk
  EXPECT_EQ(yyjson_arr_size(yyjson_obj_get(root, "account_balances")), 0u);
  EXPECT_EQ(yyjson_arr_size(yyjson_obj_get(root, "encrypted_memos")), 0u);
  EXPECT_EQ(yyjson_arr_size(yyjson_obj_get(root, "signature_pairs")), 0u);

  yyjson_doc_free(doc);
}

TEST(JsonFromRuntimeTest, RegisterAddressAndHieroAnchor) {
  Chains chains;

  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);
  tx.transaction_type = GRDT_TRANSACTION_REGISTER_ADDRESS;
  ramp(tx.register_address.user_public_key, SIGN_PUBLIC_KEY_SIZE, 0x10);
  ramp(tx.register_address.name_hash, GENERIC_HASH_SIZE, 0x20);
  ramp(tx.register_address.account_public_key, SIGN_PUBLIC_KEY_SIZE, 0x30);
  tx.address_type = GRDT_ADDRESS_COMMUNITY_HUMAN;
  tx.derivation_index = 7;

  tx.ledger_anchor.type = GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID;
  tx.ledger_anchor.hiero_transaction_id.transactionValidStart = {
      .seconds = 171627121, .nanos = 2912
  };
  tx.ledger_anchor.hiero_transaction_id.accountID = {
      .shardNum = 0, .realmNum = 0, .accountNum = 1233
  };

  hostmem_memory_block json{};
  ASSERT_EQ(
      grdm_complete_transaction_to_json(&json, &tx, GRDM_JSON_PRETTY, &chains.work, &chains.result),
      HOSTMEM_SUCCESS
  );
  // pretty means indentation, which a compact document never carries
  EXPECT_TRUE(strstr((const char *)json.data, "\n    ") != nullptr);

  yyjson_doc *doc = parse(json);
  ASSERT_TRUE(doc != nullptr);
  yyjson_val *root = yyjson_doc_get_root(doc);

  yyjson_val *address = yyjson_obj_get(root, "register_address");
  ASSERT_TRUE(address != nullptr);
  EXPECT_STREQ(getStr(address, "address_type"), "GRDT_ADDRESS_COMMUNITY_HUMAN");
  EXPECT_EQ(yyjson_get_uint(yyjson_obj_get(address, "derivation_index")), 7u);
  EXPECT_STREQ(
      getStr(address, "name_hash"),
      "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
  );

  yyjson_val *anchor = yyjson_obj_get(root, "ledger_anchor");
  ASSERT_TRUE(anchor != nullptr);
  EXPECT_STREQ(getStr(anchor, "type"), "GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID");
  yyjson_val *hiero = yyjson_obj_get(anchor, "hiero_transaction_id");
  ASSERT_TRUE(hiero != nullptr);
  EXPECT_STREQ(getStr(hiero, "transaction_valid_start"), "171627121.000002912");
  EXPECT_EQ(
      yyjson_get_sint(yyjson_obj_get(yyjson_obj_get(hiero, "account_id"), "account_num")), 1233
  );
  // a hiero anchor carries no legacy id, and the member is not written
  EXPECT_TRUE(yyjson_obj_get(anchor, "id") == nullptr);

  yyjson_doc_free(doc);
}

TEST(JsonFromRuntimeTest, CrossGroupMembersAndBodyBytes) {
  Chains chains;

  uint8_t pairingUuid[HOSTMEM_UUID_BINARY_SIZE];
  ramp(pairingUuid, HOSTMEM_UUID_BINARY_SIZE, 0xa0);
  uint8_t bodyBytes[3] = {0x0a, 0x1b, 0x2c};
  grdw_ledger_anchor pairingAnchor{};
  pairingAnchor.type = GRDT_LEDGER_ANCHOR_NODE_TRIGGER_TRANSACTION_ID;
  pairingAnchor.id = 99;

  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);
  tx.transaction_type = GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER;
  tx.previous_tx = 42;
  tx.cross_group_type = GRDT_CROSS_GROUP_INBOUND;
  tx.tx_pairing_community_uuid = pairingUuid;
  tx.pairing_ledger_anchor = &pairingAnchor;
  tx.body_bytes = {bodyBytes, sizeof(bodyBytes)};

  hostmem_memory_block json{};
  ASSERT_EQ(
      grdm_complete_transaction_to_json(
          &json, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_SUCCESS
  );

  yyjson_doc *doc = parse(json);
  ASSERT_TRUE(doc != nullptr);
  yyjson_val *root = yyjson_doc_get_root(doc);

  EXPECT_STREQ(getStr(root, "cross_group_type"), "GRDT_CROSS_GROUP_INBOUND");
  EXPECT_EQ(yyjson_get_uint(yyjson_obj_get(root, "previous_tx")), 42u);
  // a timeout leaves the detail union untouched, so no transfer object is written
  EXPECT_TRUE(yyjson_obj_get(root, "transfer") == nullptr);
  EXPECT_STREQ(getStr(root, "tx_pairing_community_uuid"), "a0a1a2a3-a4a5-a6a7-a8a9-aaabacadaeaf");
  EXPECT_STREQ(getStr(root, "body_bytes"), "0a1b2c");

  yyjson_val *pairing = yyjson_obj_get(root, "pairing_ledger_anchor");
  ASSERT_TRUE(pairing != nullptr);
  EXPECT_STREQ(getStr(pairing, "type"), "GRDT_LEDGER_ANCHOR_NODE_TRIGGER_TRANSACTION_ID");
  EXPECT_EQ(yyjson_get_uint(yyjson_obj_get(pairing, "id")), 99u);

  yyjson_doc_free(doc);
}

TEST(JsonFromRuntimeTest, UnhandledTransactionTypeIsRefused) {
  Chains chains;

  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);
  // the one type grdm_complete_transaction_from_wire() refuses to build as well
  tx.transaction_type = GRDT_TRANSACTION_COMMUNITY_FRIENDS_UPDATE;

  hostmem_memory_block json{};
  EXPECT_EQ(
      grdm_complete_transaction_to_json(
          &json, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_ERROR_ENUM_UNHANDLED
  );
  // failures leave the output untouched
  EXPECT_TRUE(json.data == nullptr);
  EXPECT_EQ(json.size, 0u);
}

TEST(JsonFromRuntimeTest, NullArgumentsAreRefused) {
  Chains chains;
  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);
  tx.transaction_type = GRDT_TRANSACTION_CREATION;
  hostmem_memory_block json{};

  EXPECT_EQ(
      grdm_complete_transaction_to_json(
          nullptr, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_ERROR_NULL_POINTER
  );
  EXPECT_EQ(
      grdm_complete_transaction_to_json(
          &json, nullptr, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_ERROR_NULL_POINTER
  );
  EXPECT_EQ(
      grdm_complete_transaction_to_json(&json, &tx, GRDM_JSON_COMPACT, nullptr, &chains.result),
      HOSTMEM_ERROR_NULL_POINTER
  );
  EXPECT_EQ(
      grdm_complete_transaction_to_json(&json, &tx, GRDM_JSON_COMPACT, &chains.work, nullptr),
      HOSTMEM_ERROR_NULL_POINTER
  );
}

/**
 * @brief A second pass over reset chains asks the host for nothing.
 *
 * This is the property the two allocators exist for: the arenas opened by the first
 * transaction serve the second one, so a stream of transactions costs the arenas once rather
 * than once per transaction. Measured through hostmem_multi_arena_measure(), which reports
 * what the chain holds from the host.
 */
TEST(JsonFromRuntimeTest, ResetChainsRenderAgainWithoutGrowing) {
  Chains chains;

  grdw_account_balance balance{};
  grdw_encrypted_memo memo{};
  grdw_signature_pair signature{};
  grdr_complete_transaction tx = makeTransfer(&balance, &memo, &signature);

  hostmem_memory_block first{};
  ASSERT_EQ(
      grdm_complete_transaction_to_json(
          &first, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_SUCCESS
  );

  hostmem_multi_arena_stats afterFirst{};
  ASSERT_EQ(hostmem_multi_arena_measure(&chains.work, &afterFirst), HOSTMEM_SUCCESS);
  ASSERT_GT(afterFirst.reserved, 0u);

  // taken before the reset: the text lives in the result chain, and the reset hands those
  // bytes to the next render rather than keeping them
  const std::string firstText((const char *)first.data, first.size);

  hostmem_multi_arena_reset(&chains.work);
  hostmem_multi_arena_reset(&chains.result);

  hostmem_memory_block second{};
  ASSERT_EQ(
      grdm_complete_transaction_to_json(
          &second, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_SUCCESS
  );
  ASSERT_EQ(firstText.size(), second.size);
  EXPECT_EQ(firstText, std::string((const char *)second.data, second.size));

  hostmem_multi_arena_stats afterSecond{};
  ASSERT_EQ(hostmem_multi_arena_measure(&chains.work, &afterSecond), HOSTMEM_SUCCESS);
  EXPECT_EQ(afterSecond.reserved, afterFirst.reserved);
  EXPECT_EQ(afterSecond.arena_count, afterFirst.arena_count);
}

/**
 * @brief A render that never reaches the host at all.
 *
 * The chains above open their arenas themselves, which means malloc the first time round.
 * Here every byte is the caller's from the start: static buffers borrowed as the arenas, and
 * an arena of its own for the descriptor vector, so there is nothing left for the chain to ask
 * the host for. What the assertions watch is exactly that -- the arena count stays at the one
 * borrowed stretch per chain, and `reserved` stays the capacity of those stretches. A chain
 * that had gone to the host for a second arena would show it in both figures.
 *
 * This is the shape a host with a fixed memory budget uses: hand the library its ground once,
 * and the rendering runs inside it for as long as the process lives.
 */
TEST(JsonFromRuntimeTest, BorrowedStorageNeverReachesTheHost) {
  // 8 byte aligned and a multiple of 8, which is what hostmem_multi_arena_borrow() insists on
  alignas(8) static uint8_t workStorage[64 * 1024];
  alignas(8) static uint8_t resultStorage[8 * 1024];
  alignas(8) static uint8_t bookkeepingStorage[4 * 1024];

  // the descriptor vectors of both chains come from here, so even the bookkeeping is ours
  hostmem bookkeeping = {};
  ASSERT_EQ(
      hostmem_init_arena_borrow(&bookkeeping, bookkeepingStorage, sizeof(bookkeepingStorage)),
      HOSTMEM_SUCCESS
  );

  hostmem_multi_arena work = {};
  hostmem_multi_arena result = {};
  ASSERT_EQ(hostmem_multi_arena_init(&work, sizeof(workStorage), 0, &bookkeeping), HOSTMEM_SUCCESS);
  ASSERT_EQ(
      hostmem_multi_arena_init(&result, sizeof(resultStorage), 0, &bookkeeping), HOSTMEM_SUCCESS
  );
  ASSERT_EQ(hostmem_multi_arena_borrow(&work, workStorage, sizeof(workStorage)), HOSTMEM_SUCCESS);
  ASSERT_EQ(
      hostmem_multi_arena_borrow(&result, resultStorage, sizeof(resultStorage)), HOSTMEM_SUCCESS
  );

  grdw_account_balance balance{};
  grdw_encrypted_memo memo{};
  grdw_signature_pair signature{};
  grdr_complete_transaction tx = makeTransfer(&balance, &memo, &signature);

  // several passes: the borrowed ground has to carry a stream of transactions, not just one
  for (int pass = 0; pass < 5; ++pass) {
    hostmem_memory_block json{};
    ASSERT_EQ(
        grdm_complete_transaction_to_json(&json, &tx, GRDM_JSON_PRETTY, &work, &result),
        HOSTMEM_SUCCESS
    ) << "pass "
      << pass;
    ASSERT_GT(json.size, 0u);

    hostmem_multi_arena_stats workStats{};
    hostmem_multi_arena_stats resultStats{};
    ASSERT_EQ(hostmem_multi_arena_measure(&work, &workStats), HOSTMEM_SUCCESS);
    ASSERT_EQ(hostmem_multi_arena_measure(&result, &resultStats), HOSTMEM_SUCCESS);

    // one arena each, and it is the borrowed one: nothing was opened from the host
    EXPECT_EQ(workStats.arena_count, 1u) << "pass " << pass;
    EXPECT_EQ(workStats.reserved, sizeof(workStorage)) << "pass " << pass;
    EXPECT_EQ(resultStats.arena_count, 1u) << "pass " << pass;
    EXPECT_EQ(resultStats.reserved, sizeof(resultStorage)) << "pass " << pass;

    hostmem_multi_arena_reset(&work);
    hostmem_multi_arena_reset(&result);
  }

  // borrowed arenas are let go untouched, and the bookkeeping arena is ours as well
  hostmem_multi_arena_release(&work);
  hostmem_multi_arena_release(&result);
  hostmem_release(&bookkeeping);
}

// ==========================================================================================
//  The wire view: the same data, in the shape it arrives in
// ==========================================================================================

namespace {

//! A transfer body with one memo, filled so every member is recognizable in the text.
grdw_transaction_body makeTransferBody(grdw_encrypted_memo *memo) {
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.created_at = {.seconds = 1700000000, .nanos = 2912};
  body.transaction_type = GRDT_TRANSACTION_TRANSFER;
  body.type = GRDT_CROSS_GROUP_LOCAL;
  ramp(body.transfer.sender.pubkey, SIGN_PUBLIC_KEY_SIZE, 0x10);
  body.transfer.sender.amount = 12345;
  memcpy(body.transfer.sender.community_uuid, kCommunityUuid, HOSTMEM_UUID_BINARY_SIZE);
  ramp(body.transfer.recipient, SIGN_PUBLIC_KEY_SIZE, 0x40);
  body.memos = memo;
  body.memos_count = 1;
  return body;
}

grdw_confirmed_transaction makeConfirmed(
    grdw_account_balance *balance, grdw_signature_pair *signature, hostmem_memory_block bodyBytes
) {
  grdw_confirmed_transaction tx;
  grdw_confirmed_transaction_init(&tx);
  tx.id = 121;
  tx.confirmed_at = {.seconds = 1700000060, .nanos = 0};
  ramp(tx.running_hash, GENERIC_HASH_SIZE, 0x80);
  tx.ledger_anchor.type = GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_ID;
  tx.ledger_anchor.id = 4711;
  tx.balance_derivation = GRDT_BALANCE_DERIVATION_NODE;
  tx.account_balances = balance;
  tx.account_balances_count = 1;
  tx.transaction.sig_map = signature;
  tx.transaction.sig_map_count = 1;
  tx.transaction.body_bytes = bodyBytes;
  return tx;
}

} // namespace

TEST(JsonFromWireTest, TransferBodyKeepsTheWireShape) {
  Chains chains;
  uint8_t memoBytes[4] = {0xde, 0xad, 0xbe, 0xef};
  grdw_encrypted_memo memo{};
  memo.type = GRDT_MEMO_KEY_SHARED_SECRET;
  memo.memo = {memoBytes, sizeof(memoBytes)};
  grdw_transaction_body body = makeTransferBody(&memo);

  hostmem_memory_block json{};
  ASSERT_EQ(
      grdm_transaction_body_to_json(&json, &body, GRDM_JSON_COMPACT, &chains.work, &chains.result),
      HOSTMEM_SUCCESS
  );
  yyjson_doc *doc = parse(json);
  ASSERT_TRUE(doc != nullptr);
  yyjson_val *root = yyjson_doc_get_root(doc);

  EXPECT_STREQ(getStr(root, "transaction_type"), "GRDT_TRANSACTION_TRANSFER");
  EXPECT_STREQ(getStr(root, "cross_group_type"), "GRDT_CROSS_GROUP_LOCAL");
  EXPECT_STREQ(getStr(root, "created_at"), "1700000000.000002912");
  // a local body names no other community
  EXPECT_TRUE(yyjson_obj_get(root, "other_community_uuid") == nullptr);

  yyjson_val *transfer = yyjson_obj_get(root, "transfer");
  ASSERT_TRUE(transfer != nullptr);
  // the sender stays one object here, unlike the runtime view that spreads it out
  yyjson_val *sender = yyjson_obj_get(transfer, "sender");
  ASSERT_TRUE(sender != nullptr);
  EXPECT_STREQ(
      getStr(sender, "pubkey"), "101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f"
  );
  EXPECT_STREQ(getStr(sender, "amount"), "1.2345");
  EXPECT_STREQ(getStr(sender, "community_uuid"), "019e2c31-a303-75c0-941e-f35c59e4f978");
  // and the recipient is a bare key, which is all the wire carries for it
  EXPECT_STREQ(
      getStr(transfer, "recipient"),
      "404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f"
  );

  yyjson_val *memos = yyjson_obj_get(root, "memos");
  ASSERT_EQ(yyjson_arr_size(memos), 1u);
  EXPECT_STREQ(getStr(yyjson_arr_get(memos, 0), "memo"), "deadbeef");

  yyjson_doc_free(doc);
}

/**
 * @brief Every branch of the wire union reaches the text under its own name, carrying its own
 *        value.
 *
 * The union is read through transaction_type, so a branch that reads the wrong member would
 * still produce plausible looking output -- and on a zeroed body it would produce *identical*
 * output, which is why each case here sets a value only its own branch can show and then looks
 * for exactly that value. Checking that the member name appeared would not have been enough:
 * it lets a creation rendered from the transfer member pass.
 */
TEST(JsonFromWireTest, EveryPayloadTypeIsDescribedWithItsOwnValue) {
  struct Case {
    grdt_transaction type;
    const char *key;
    std::function<void(grdw_transaction_body &)> fill;
    std::function<void(yyjson_val *)> expect;
  };

  const Case cases[] = {
      {GRDT_TRANSACTION_TRANSFER, "transfer",
       [](grdw_transaction_body &b) { b.transfer.sender.amount = 11111; },
       [](yyjson_val *p) {
         EXPECT_STREQ(getStr(yyjson_obj_get(p, "sender"), "amount"), "1.1111");
       }},

      {GRDT_TRANSACTION_CREATION, "creation",
       [](grdw_transaction_body &b) {
         b.creation.recipient.amount = 22222;
         b.creation.target_date.seconds = 1700000000;
       },
       [](yyjson_val *p) {
         EXPECT_STREQ(getStr(yyjson_obj_get(p, "recipient"), "amount"), "2.2222");
         EXPECT_EQ(yyjson_get_sint(yyjson_obj_get(p, "target_date")), 1700000000);
       }},

      {GRDT_TRANSACTION_COMMUNITY_FRIENDS_UPDATE, "community_friends_update",
       [](grdw_transaction_body &b) { b.community_friends_update.color_fusion = true; },
       [](yyjson_val *p) { EXPECT_TRUE(yyjson_get_bool(yyjson_obj_get(p, "color_fusion"))); }},

      {GRDT_TRANSACTION_REGISTER_ADDRESS, "register_address",
       [](grdw_transaction_body &b) {
         b.register_address.derivation_index = 7;
         b.register_address.address_type = GRDT_ADDRESS_COMMUNITY_HUMAN;
         ramp(b.register_address.name_hash, GENERIC_HASH_SIZE, 0x20);
       },
       [](yyjson_val *p) {
         EXPECT_EQ(yyjson_get_uint(yyjson_obj_get(p, "derivation_index")), 7u);
         EXPECT_STREQ(getStr(p, "address_type"), "GRDT_ADDRESS_COMMUNITY_HUMAN");
         EXPECT_STREQ(
             getStr(p, "name_hash"),
             "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
         );
       }},

      {GRDT_TRANSACTION_DEFERRED_TRANSFER, "deferred_transfer",
       [](grdw_transaction_body &b) {
         b.deferred_transfer.timeout_duration = 3600;
         b.deferred_transfer.transfer.sender.amount = 33333;
       },
       [](yyjson_val *p) {
         EXPECT_EQ(yyjson_get_uint(yyjson_obj_get(p, "timeout_duration")), 3600u);
         EXPECT_STREQ(
             getStr(yyjson_obj_get(yyjson_obj_get(p, "transfer"), "sender"), "amount"), "3.3333"
         );
       }},

      {GRDT_TRANSACTION_COMMUNITY_ROOT, "community_root",
       [](grdw_transaction_body &b) {
         ramp(b.community_root.gmw_pubkey, SIGN_PUBLIC_KEY_SIZE, 0x50);
       },
       [](yyjson_val *p) {
         EXPECT_STREQ(
             getStr(p, "gmw_pubkey"),
             "505152535455565758595a5b5c5d5e5f606162636465666768696a6b6c6d6e6f"
         );
       }},

      {GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER, "redeem_deferred_transfer",
       [](grdw_transaction_body &b) {
         b.redeem_deferred_transfer.deferred_transfer_transaction_nr = 42;
         b.redeem_deferred_transfer.transfer.sender.amount = 44444;
       },
       [](yyjson_val *p) {
         EXPECT_EQ(yyjson_get_uint(yyjson_obj_get(p, "deferred_transfer_transaction_nr")), 42u);
         EXPECT_STREQ(
             getStr(yyjson_obj_get(yyjson_obj_get(p, "transfer"), "sender"), "amount"), "4.4444"
         );
       }},

      {GRDT_TRANSACTION_TIMEOUT_DEFERRED_TRANSFER, "timeout_deferred_transfer",
       [](grdw_transaction_body &b) {
         b.timeout_deferred_transfer.deferred_transfer_transaction_nr = 43;
       },
       [](yyjson_val *p) {
         EXPECT_EQ(yyjson_get_uint(yyjson_obj_get(p, "deferred_transfer_transaction_nr")), 43u);
         // a timeout carries the number and nothing else; a transfer member here would mean the
         // wrong branch was read
         EXPECT_TRUE(yyjson_obj_get(p, "transfer") == nullptr);
       }},
  };

  for (const Case &testCase : cases) {
    Chains chains;
    grdw_transaction_body body;
    grdw_transaction_body_init(&body);
    body.transaction_type = testCase.type;
    testCase.fill(body);

    hostmem_memory_block json{};
    ASSERT_EQ(
        grdm_transaction_body_to_json(
            &json, &body, GRDM_JSON_COMPACT, &chains.work, &chains.result
        ),
        HOSTMEM_SUCCESS
    ) << testCase.key;

    yyjson_doc *doc = parse(json);
    ASSERT_TRUE(doc != nullptr) << testCase.key;
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *payload = yyjson_obj_get(root, testCase.key);
    ASSERT_TRUE(payload != nullptr) << "missing payload member: " << testCase.key;
    EXPECT_STREQ(getStr(root, "transaction_type"), grdt_transaction_to_string(testCase.type));
    testCase.expect(payload);
    yyjson_doc_free(doc);
  }
}

TEST(JsonFromWireTest, PayloadDetailsThatOnlyTheWireCarries) {
  Chains chains;
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.transaction_type = GRDT_TRANSACTION_DEFERRED_TRANSFER;
  body.deferred_transfer.timeout_duration = 3600;
  body.deferred_transfer.transfer.sender.amount = 250000; // 25.0000 GDD
  uint8_t otherCommunity[HOSTMEM_UUID_BINARY_SIZE];
  ramp(otherCommunity, HOSTMEM_UUID_BINARY_SIZE, 0xa0);
  body.other_community_uuid = otherCommunity;
  body.type = GRDT_CROSS_GROUP_OUTBOUND;

  hostmem_memory_block json{};
  ASSERT_EQ(
      grdm_transaction_body_to_json(&json, &body, GRDM_JSON_COMPACT, &chains.work, &chains.result),
      HOSTMEM_SUCCESS
  );
  yyjson_doc *doc = parse(json);
  ASSERT_TRUE(doc != nullptr);
  yyjson_val *root = yyjson_doc_get_root(doc);

  EXPECT_STREQ(getStr(root, "cross_group_type"), "GRDT_CROSS_GROUP_OUTBOUND");
  EXPECT_STREQ(getStr(root, "other_community_uuid"), "a0a1a2a3-a4a5-a6a7-a8a9-aaabacadaeaf");

  yyjson_val *deferred = yyjson_obj_get(root, "deferred_transfer");
  ASSERT_TRUE(deferred != nullptr);
  EXPECT_EQ(yyjson_get_uint(yyjson_obj_get(deferred, "timeout_duration")), 3600u);
  yyjson_val *sender = yyjson_obj_get(yyjson_obj_get(deferred, "transfer"), "sender");
  ASSERT_TRUE(sender != nullptr);
  EXPECT_STREQ(getStr(sender, "amount"), "25.0000");

  yyjson_doc_free(doc);
}

TEST(JsonFromWireTest, TransactionTypeNoneIsRefused) {
  Chains chains;
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  // init leaves GRDT_TRANSACTION_NONE, which names no union member at all
  ASSERT_EQ(body.transaction_type, GRDT_TRANSACTION_NONE);

  hostmem_memory_block json{};
  EXPECT_EQ(
      grdm_transaction_body_to_json(&json, &body, GRDM_JSON_COMPACT, &chains.work, &chains.result),
      HOSTMEM_ERROR_ENUM_UNHANDLED
  );
  EXPECT_TRUE(json.data == nullptr);
}

TEST(JsonFromWireTest, GradidoTransactionLeavesEmptyMembersOut) {
  Chains chains;
  grdw_gradido_transaction tx;
  grdw_gradido_transaction_init(&tx);

  hostmem_memory_block json{};
  ASSERT_EQ(
      grdm_gradido_transaction_to_json(&json, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result),
      HOSTMEM_SUCCESS
  );
  yyjson_doc *doc = parse(json);
  ASSERT_TRUE(doc != nullptr);
  yyjson_val *root = yyjson_doc_get_root(doc);

  // the array is written even when empty, the two optional members are not
  EXPECT_EQ(yyjson_arr_size(yyjson_obj_get(root, "sig_map")), 0u);
  EXPECT_TRUE(yyjson_obj_get(root, "body_bytes") == nullptr);
  EXPECT_TRUE(yyjson_obj_get(root, "pairing_ledger_anchor") == nullptr);

  yyjson_doc_free(doc);
}

TEST(JsonFromWireTest, ConfirmedTransactionNestsTheGradidoTransaction) {
  Chains chains;
  uint8_t bodyBytes[6] = {0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f};
  grdw_account_balance balance{};
  ramp(balance.pubkey, SIGN_PUBLIC_KEY_SIZE, 0x40);
  balance.balance = 987650000;
  memcpy(balance.community_uuid, kCommunityUuid, HOSTMEM_UUID_BINARY_SIZE);
  grdw_signature_pair signature{};
  ramp(signature.public_key, SIGN_PUBLIC_KEY_SIZE, 0x10);
  ramp(signature.signature, SIGN_SIGNATURE_SIZE, 0x01);

  grdw_confirmed_transaction tx =
      makeConfirmed(&balance, &signature, {bodyBytes, sizeof(bodyBytes)});

  hostmem_memory_block json{};
  ASSERT_EQ(
      grdm_confirmed_transaction_to_json(
          &json, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_SUCCESS
  );
  yyjson_doc *doc = parse(json);
  ASSERT_TRUE(doc != nullptr);
  yyjson_val *root = yyjson_doc_get_root(doc);

  EXPECT_EQ(yyjson_get_uint(yyjson_obj_get(root, "id")), 121u);
  EXPECT_STREQ(getStr(root, "confirmed_at"), "1700000060.000000000");
  EXPECT_STREQ(getStr(root, "balance_derivation"), "GRDT_BALANCE_DERIVATION_NODE");
  EXPECT_EQ(yyjson_arr_size(yyjson_obj_get(root, "account_balances")), 1u);

  // the gradido transaction is a member, not flattened into the root
  yyjson_val *nested = yyjson_obj_get(root, "transaction");
  ASSERT_TRUE(nested != nullptr);
  EXPECT_STREQ(getStr(nested, "body_bytes"), "0a1b2c3d4e5f");
  EXPECT_EQ(yyjson_arr_size(yyjson_obj_get(nested, "sig_map")), 1u);
  // and its members are not also in the root
  EXPECT_TRUE(yyjson_obj_get(root, "body_bytes") == nullptr);
  EXPECT_TRUE(yyjson_obj_get(root, "sig_map") == nullptr);

  yyjson_doc_free(doc);
}

/**
 * @brief The two views agree wherever they describe the same thing.
 *
 * A wire transaction and the runtime transaction built from it are rendered separately, and the
 * members both carry -- the anchor, the balances, the amount -- are compared as text. They come
 * from one place in json_writer.c, and this is what would catch it if a second copy ever
 * appeared and one of them changed.
 */
TEST(JsonFromWireTest, WireAndRuntimeViewsSpellSharedFieldsAlike) {
  Chains chains;
  grdw_account_balance balance{};
  ramp(balance.pubkey, SIGN_PUBLIC_KEY_SIZE, 0x40);
  balance.balance = 987650000;
  memcpy(balance.community_uuid, kCommunityUuid, HOSTMEM_UUID_BINARY_SIZE);
  grdw_signature_pair signature{};
  ramp(signature.public_key, SIGN_PUBLIC_KEY_SIZE, 0x10);
  ramp(signature.signature, SIGN_SIGNATURE_SIZE, 0x01);
  grdw_encrypted_memo memo{};
  uint8_t memoBytes[4] = {0xde, 0xad, 0xbe, 0xef};
  memo.type = GRDT_MEMO_KEY_SHARED_SECRET;
  memo.memo = {memoBytes, sizeof(memoBytes)};

  grdw_transaction_body body = makeTransferBody(&memo);
  // real body bytes: grdm_complete_transaction_from_wire() clones them unconditionally and
  // refuses a NULL block, so an empty one would fail the conversion rather than this comparison
  uint8_t bodyBytes[6] = {0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f};
  grdw_confirmed_transaction wire =
      makeConfirmed(&balance, &signature, {bodyBytes, sizeof(bodyBytes)});
  wire.ledger_anchor.type = GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID;
  wire.ledger_anchor.hiero_transaction_id.transactionValidStart = {
      .seconds = 171627121, .nanos = 2912
  };
  wire.ledger_anchor.hiero_transaction_id.accountID = {
      .shardNum = 0, .realmNum = 0, .accountNum = 1233
  };

  grdr_complete_transaction runtime;
  grdr_complete_transaction_init(&runtime);
  ASSERT_EQ(
      grdm_complete_transaction_from_wire(&runtime, &body, &wire, kCommunityUuid), HOSTMEM_SUCCESS
  );

  hostmem_memory_block wireJson{};
  ASSERT_EQ(
      grdm_confirmed_transaction_to_json(
          &wireJson, &wire, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_SUCCESS
  );
  const std::string wireText((const char *)wireJson.data, wireJson.size);
  hostmem_multi_arena_reset(&chains.work);
  hostmem_multi_arena_reset(&chains.result);

  hostmem_memory_block runtimeJson{};
  ASSERT_EQ(
      grdm_complete_transaction_to_json(
          &runtimeJson, &runtime, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_SUCCESS
  );

  yyjson_doc *wireDoc = yyjson_read(wireText.c_str(), wireText.size(), 0);
  yyjson_doc *runtimeDoc = parse(runtimeJson);
  ASSERT_TRUE(wireDoc != nullptr);
  ASSERT_TRUE(runtimeDoc != nullptr);
  yyjson_val *wireRoot = yyjson_doc_get_root(wireDoc);
  yyjson_val *runtimeRoot = yyjson_doc_get_root(runtimeDoc);

  // the anchor: same member names, same rendering, in both views
  yyjson_val *wireAnchor = yyjson_obj_get(wireRoot, "ledger_anchor");
  yyjson_val *runtimeAnchor = yyjson_obj_get(runtimeRoot, "ledger_anchor");
  ASSERT_TRUE(wireAnchor != nullptr);
  ASSERT_TRUE(runtimeAnchor != nullptr);
  EXPECT_TRUE(yyjson_equals(wireAnchor, runtimeAnchor));

  // the balances, element for element
  EXPECT_TRUE(yyjson_equals(
      yyjson_obj_get(wireRoot, "account_balances"), yyjson_obj_get(runtimeRoot, "account_balances")
  ));

  // the amount reaches both views through different members and comes out spelled the same
  yyjson_val *wireSender = nullptr; // the wire view keeps it in the body, rendered separately
  (void)wireSender;
  EXPECT_STREQ(getStr(yyjson_obj_get(runtimeRoot, "transfer"), "amount"), "1.2345");
  EXPECT_STREQ(getStr(wireRoot, "confirmed_at"), getStr(runtimeRoot, "confirmed_at"));

  yyjson_doc_free(wireDoc);
  yyjson_doc_free(runtimeDoc);
  grdr_complete_transaction_release(&runtime);
}

TEST(JsonFromWireTest, NullArgumentsAreRefused) {
  Chains chains;
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  body.transaction_type = GRDT_TRANSACTION_TRANSFER;
  grdw_gradido_transaction tx;
  grdw_gradido_transaction_init(&tx);
  grdw_confirmed_transaction confirmed;
  grdw_confirmed_transaction_init(&confirmed);
  hostmem_memory_block json{};

  EXPECT_EQ(
      grdm_transaction_body_to_json(
          nullptr, &body, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_ERROR_NULL_POINTER
  );
  EXPECT_EQ(
      grdm_transaction_body_to_json(
          &json, nullptr, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_ERROR_NULL_POINTER
  );
  EXPECT_EQ(
      grdm_transaction_body_to_json(&json, &body, GRDM_JSON_COMPACT, nullptr, &chains.result),
      HOSTMEM_ERROR_NULL_POINTER
  );
  EXPECT_EQ(
      grdm_gradido_transaction_to_json(&json, &tx, GRDM_JSON_COMPACT, &chains.work, nullptr),
      HOSTMEM_ERROR_NULL_POINTER
  );
  EXPECT_EQ(
      grdm_confirmed_transaction_to_json(
          &json, nullptr, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_ERROR_NULL_POINTER
  );
}

// ==========================================================================================
//  body_detail: the same bytes, sealed or opened
// ==========================================================================================

namespace {

/**
 * @brief What the body tests below hand pbtools.
 *
 * Generous for a body of this size, and a fixed figure on purpose: the point of the parameter is
 * that the caller names it, so a test that let it be guessed would be testing something else.
 */
constexpr uint32_t kPbWorkspace = 8192;

/**
 * @brief A real encoded transaction body, so the decoding mode has something that decodes.
 *
 * Encoding here rather than pasting a fixture keeps the bytes and the expectations in step: the
 * body below is the one the assertions look for, and a schema change moves both at once.
 */
struct EncodedBody {
  hostmem_multi_arena arena{};
  uint8_t buffer[512]{};
  hostmem_memory_block bytes{};

  EncodedBody() {
    EXPECT_EQ(hostmem_multi_arena_init(&arena, 4096, 0, nullptr), HOSTMEM_SUCCESS);
    grdw_transaction_body body;
    grdw_transaction_body_init(&body);
    body.created_at = {.seconds = 1700000000, .nanos = 2912};
    body.transaction_type = GRDT_TRANSACTION_TRANSFER;
    body.type = GRDT_CROSS_GROUP_LOCAL;
    ramp(body.transfer.sender.pubkey, SIGN_PUBLIC_KEY_SIZE, 0x10);
    body.transfer.sender.amount = 12345;
    memcpy(body.transfer.sender.community_uuid, kCommunityUuid, HOSTMEM_UUID_BINARY_SIZE);
    ramp(body.transfer.recipient, SIGN_PUBLIC_KEY_SIZE, 0x40);

    hostmem_memory_block destination = {buffer, sizeof(buffer)};
    int finalSize = 0;
    alignas(8) static uint8_t workspaceBytes[4096];
    hostmem_memory_block workspace = {workspaceBytes, sizeof(workspaceBytes)};
    EXPECT_EQ(
        grdw_transaction_body_encode(&destination, &finalSize, &body, &workspace), HOSTMEM_SUCCESS
    );
    bytes = {buffer, (uint32_t)finalSize};
  }
  ~EncodedBody() {
    hostmem_multi_arena_release(&arena);
  }
};

} // namespace

TEST(JsonBodyDetailTest, HexAndDecodedAreAlternatives) {
  EncodedBody encoded;
  ASSERT_GT(encoded.bytes.size, 0u);

  grdw_gradido_transaction tx;
  grdw_gradido_transaction_init(&tx);
  tx.body_bytes = encoded.bytes;

  // --- hex: the octets, under body_bytes ---
  {
    Chains chains;
    hostmem_memory_block json{};
    ASSERT_EQ(
        grdm_gradido_transaction_to_json(
            &json, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result
        ),
        HOSTMEM_SUCCESS
    );
    yyjson_doc *doc = parse(json);
    ASSERT_TRUE(doc != nullptr);
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *hex = getStr(root, "body_bytes");
    ASSERT_TRUE(hex != nullptr);
    EXPECT_EQ(strlen(hex), encoded.bytes.size * 2u);
    // the two members are alternatives, never both
    EXPECT_TRUE(yyjson_obj_get(root, "body") == nullptr);
    yyjson_doc_free(doc);
  }

  // --- decoded: the contents, under body ---
  {
    Chains chains;
    hostmem_memory_block json{};
    ASSERT_EQ(
        grdm_gradido_transaction_with_body_to_json(
            &json, &tx, GRDM_JSON_COMPACT, kPbWorkspace, &chains.work, &chains.result
        ),
        HOSTMEM_SUCCESS
    );
    yyjson_doc *doc = parse(json);
    ASSERT_TRUE(doc != nullptr);
    yyjson_val *root = yyjson_doc_get_root(doc);
    EXPECT_TRUE(yyjson_obj_get(root, "body_bytes") == nullptr);

    yyjson_val *body = yyjson_obj_get(root, "body");
    ASSERT_TRUE(body != nullptr);
    // the decoded object is the same shape grdm_transaction_body_to_json() produces
    EXPECT_STREQ(getStr(body, "transaction_type"), "GRDT_TRANSACTION_TRANSFER");
    EXPECT_STREQ(getStr(body, "created_at"), "1700000000.000002912");
    yyjson_val *sender = yyjson_obj_get(yyjson_obj_get(body, "transfer"), "sender");
    ASSERT_TRUE(sender != nullptr);
    EXPECT_STREQ(getStr(sender, "amount"), "1.2345");
    EXPECT_STREQ(
        getStr(sender, "pubkey"), "101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f"
    );
    yyjson_doc_free(doc);
  }
}

/**
 * @brief The decoded body is spelled exactly the way the standalone body mapping spells it.
 *
 * Both go through build_body(), and this is what would notice if the nested path ever grew a
 * second rendering of its own.
 */
TEST(JsonBodyDetailTest, NestedBodyMatchesTheStandaloneBodyMapping) {
  EncodedBody encoded;
  Chains chains;

  grdw_gradido_transaction tx;
  grdw_gradido_transaction_init(&tx);
  tx.body_bytes = encoded.bytes;

  hostmem_memory_block nestedJson{};
  ASSERT_EQ(
      grdm_gradido_transaction_with_body_to_json(
          &nestedJson, &tx, GRDM_JSON_COMPACT, kPbWorkspace, &chains.work, &chains.result
      ),
      HOSTMEM_SUCCESS
  );
  const std::string nestedText((const char *)nestedJson.data, nestedJson.size);
  hostmem_multi_arena_reset(&chains.work);
  hostmem_multi_arena_reset(&chains.result);

  // decode the same bytes by hand and render them through the body mapping
  hostmem_multi_arena decodeArena{};
  ASSERT_EQ(hostmem_multi_arena_init(&decodeArena, 8192, 0, nullptr), HOSTMEM_SUCCESS);
  grdw_transaction_body body;
  grdw_transaction_body_init(&body);
  alignas(8) static uint8_t workspaceBytes[8192];
  hostmem_memory_block workspace = {workspaceBytes, sizeof(workspaceBytes)};
  ASSERT_EQ(
      grdw_transaction_body_decode(&body, &encoded.bytes, &workspace, &decodeArena), HOSTMEM_SUCCESS
  );

  hostmem_memory_block standaloneJson{};
  ASSERT_EQ(
      grdm_transaction_body_to_json(
          &standaloneJson, &body, GRDM_JSON_COMPACT, &chains.work, &chains.result
      ),
      HOSTMEM_SUCCESS
  );

  yyjson_doc *nestedDoc = yyjson_read(nestedText.c_str(), nestedText.size(), 0);
  yyjson_doc *standaloneDoc = parse(standaloneJson);
  ASSERT_TRUE(nestedDoc != nullptr);
  ASSERT_TRUE(standaloneDoc != nullptr);
  EXPECT_TRUE(yyjson_equals(
      yyjson_obj_get(yyjson_doc_get_root(nestedDoc), "body"), yyjson_doc_get_root(standaloneDoc)
  ));

  yyjson_doc_free(nestedDoc);
  yyjson_doc_free(standaloneDoc);
  hostmem_multi_arena_release(&decodeArena);
}

TEST(JsonBodyDetailTest, ConfirmedTransactionPassesTheChoiceDown) {
  EncodedBody encoded;
  grdw_account_balance balance{};
  grdw_signature_pair signature{};
  grdw_confirmed_transaction tx = makeConfirmed(&balance, &signature, encoded.bytes);

  for (int decoded = 0; decoded < 2; ++decoded) {
    Chains chains;
    hostmem_memory_block json{};
    ASSERT_EQ(
        decoded ? grdm_confirmed_transaction_with_body_to_json(
                      &json, &tx, GRDM_JSON_COMPACT, kPbWorkspace, &chains.work, &chains.result
                  )
                : grdm_confirmed_transaction_to_json(
                      &json, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result
                  ),
        HOSTMEM_SUCCESS
    ) << "decoded="
      << decoded;

    yyjson_doc *doc = parse(json);
    ASSERT_TRUE(doc != nullptr);
    // the choice reaches the nested transaction, which is where the body lives
    yyjson_val *nested = yyjson_obj_get(yyjson_doc_get_root(doc), "transaction");
    ASSERT_TRUE(nested != nullptr);
    if (decoded) {
      EXPECT_TRUE(yyjson_obj_get(nested, "body") != nullptr);
      EXPECT_TRUE(yyjson_obj_get(nested, "body_bytes") == nullptr);
    } else {
      EXPECT_TRUE(yyjson_obj_get(nested, "body_bytes") != nullptr);
      EXPECT_TRUE(yyjson_obj_get(nested, "body") == nullptr);
    }
    yyjson_doc_free(doc);
  }
}

TEST(JsonBodyDetailTest, BytesThatAreNotABodyAreRefused) {
  Chains chains;
  // a run of 0xff is not a protobuf message; asking for the contents has to say so rather than
  // fall back to hex, which would hide the failure behind output that looks fine
  uint8_t garbage[32];
  memset(garbage, 0xff, sizeof(garbage));

  grdw_gradido_transaction tx;
  grdw_gradido_transaction_init(&tx);
  tx.body_bytes = {garbage, sizeof(garbage)};

  hostmem_memory_block json{};
  hostmem_result decodedResult = grdm_gradido_transaction_with_body_to_json(
      &json, &tx, GRDM_JSON_COMPACT, kPbWorkspace, &chains.work, &chains.result
  );
  EXPECT_NE(decodedResult, HOSTMEM_SUCCESS);
  EXPECT_TRUE(json.data == nullptr);

  // the same bytes as hex are no trouble at all: nothing is being interpreted there
  ASSERT_EQ(
      grdm_gradido_transaction_to_json(&json, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result),
      HOSTMEM_SUCCESS
  );
  yyjson_doc *doc = parse(json);
  ASSERT_TRUE(doc != nullptr);
  EXPECT_STREQ(
      getStr(yyjson_doc_get_root(doc), "body_bytes"),
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
  );
  yyjson_doc_free(doc);
}

TEST(JsonBodyDetailTest, NoBodyBytesMeansNoMemberInEitherMode) {
  grdw_gradido_transaction tx;
  grdw_gradido_transaction_init(&tx);

  for (int decoded = 0; decoded < 2; ++decoded) {
    Chains chains;
    hostmem_memory_block json{};
    ASSERT_EQ(
        decoded ? grdm_gradido_transaction_with_body_to_json(
                      &json, &tx, GRDM_JSON_COMPACT, kPbWorkspace, &chains.work, &chains.result
                  )
                : grdm_gradido_transaction_to_json(
                      &json, &tx, GRDM_JSON_COMPACT, &chains.work, &chains.result
                  ),
        HOSTMEM_SUCCESS
    ) << "decoded="
      << decoded;
    yyjson_doc *doc = parse(json);
    ASSERT_TRUE(doc != nullptr);
    yyjson_val *root = yyjson_doc_get_root(doc);
    EXPECT_TRUE(yyjson_obj_get(root, "body_bytes") == nullptr);
    EXPECT_TRUE(yyjson_obj_get(root, "body") == nullptr);
    yyjson_doc_free(doc);
  }
}

/**
 * @brief The size is the caller's, and a stretch too small says exactly that.
 *
 * This is what the parameter is for: a render that comes back with OUT_OF_MEMORY has told the
 * caller to raise the figure, not that the body is bad. A caller reading a stream does that
 * once and keeps the larger figure for everything after -- which is what the second half here
 * stands in for.
 */
TEST(JsonBodyDetailTest, TooSmallWorkspaceIsReportedAndALargerOneWorks) {
  EncodedBody encoded;
  Chains chains;

  grdw_gradido_transaction tx;
  grdw_gradido_transaction_init(&tx);
  tx.body_bytes = encoded.bytes;

  hostmem_memory_block json{};
  EXPECT_EQ(
      grdm_gradido_transaction_with_body_to_json(
          &json, &tx, GRDM_JSON_COMPACT, 64, &chains.work, &chains.result
      ),
      HOSTMEM_ERROR_OUT_OF_MEMORY
  ) << "64 bytes cannot hold this body, and saying so is the whole point of the parameter";
  EXPECT_TRUE(json.data == nullptr);

  // nothing else changes -- same transaction, same chains, a larger figure
  hostmem_multi_arena_reset(&chains.work);
  hostmem_multi_arena_reset(&chains.result);
  ASSERT_EQ(
      grdm_gradido_transaction_with_body_to_json(
          &json, &tx, GRDM_JSON_COMPACT, kPbWorkspace, &chains.work, &chains.result
      ),
      HOSTMEM_SUCCESS
  );
  yyjson_doc *doc = parse(json);
  ASSERT_TRUE(doc != nullptr);
  EXPECT_TRUE(yyjson_obj_get(yyjson_doc_get_root(doc), "body") != nullptr);
  yyjson_doc_free(doc);

  // and a size of zero is a caller mistake rather than a size worth trying
  EXPECT_EQ(
      grdm_gradido_transaction_with_body_to_json(
          &json, &tx, GRDM_JSON_COMPACT, 0, &chains.work, &chains.result
      ),
      HOSTMEM_ERROR_INVALID_PARAM
  );
  EXPECT_EQ(
      grdm_confirmed_transaction_with_body_to_json(
          &json, nullptr, GRDM_JSON_COMPACT, kPbWorkspace, &chains.work, &chains.result
      ),
      HOSTMEM_ERROR_NULL_POINTER
  );
}

/**
 * @brief The workspace comes out of the render's own work chain, not from somewhere else.
 *
 * A caller opens two allocators for a render and should not have to open a third for the decode
 * inside it. Measured through the chain: asking for a larger stretch makes the work chain hold
 * more, which is only true if that is where it came from.
 */
TEST(JsonBodyDetailTest, TheDecodeDrawsFromTheWorkChain) {
  EncodedBody encoded;
  grdw_gradido_transaction tx;
  grdw_gradido_transaction_init(&tx);
  tx.body_bytes = encoded.bytes;

  auto usedForWorkspace = [&](uint32_t size) {
    Chains chains;
    hostmem_memory_block json{};
    EXPECT_EQ(
        grdm_gradido_transaction_with_body_to_json(
            &json, &tx, GRDM_JSON_COMPACT, size, &chains.work, &chains.result
        ),
        HOSTMEM_SUCCESS
    );
    hostmem_multi_arena_stats stats{};
    EXPECT_EQ(hostmem_multi_arena_measure(&chains.work, &stats), HOSTMEM_SUCCESS);
    return stats.used;
  };

  const uint64_t small = usedForWorkspace(kPbWorkspace);
  const uint64_t large = usedForWorkspace(kPbWorkspace * 4);
  EXPECT_GE(large - small, (uint64_t)kPbWorkspace * 3)
      << "the extra bytes have to show up in the work chain, which is where they are taken from";
}
