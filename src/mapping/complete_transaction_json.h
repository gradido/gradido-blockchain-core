#ifndef GRADIDO_BLOCKCHAIN_CORE_SRC_MAPPING_COMPLETE_TRANSACTION_JSON_H
#define GRADIDO_BLOCKCHAIN_CORE_SRC_MAPPING_COMPLETE_TRANSACTION_JSON_H

/**
 * @file complete_transaction_json.h
 * @brief The member names both JSON directions of grdr_complete_transaction spell.
 *
 * Private to src/mapping/ and not installed: a consumer reads the shape from
 * mapping/json_from_runtime.h, which documents it, and never has to name a key itself.
 *
 * The two mappers are each other's inverse, and a key spelled in only one of them would break
 * that quietly -- a field written and never read back reappears as a default. So every name
 * lives here once and both sides take it from the same place; a rename moves both banks of the
 * river at the same moment.
 *
 * The names follow the fields of @ref grdr_complete_transaction, so a reader of the struct
 * recognises a document without a table, and a reader of a document finds the field without a
 * search.
 */

// --- envelope ---------------------------------------------------------------------------
#define GRDM_JSON_KEY_TX_NR "tx_nr"
#define GRDM_JSON_KEY_CONFIRMED_AT "confirmed_at"
#define GRDM_JSON_KEY_CREATED_AT "created_at"
#define GRDM_JSON_KEY_TX_COMMUNITY_UUID "tx_community_uuid"
#define GRDM_JSON_KEY_LEDGER_ANCHOR "ledger_anchor"
#define GRDM_JSON_KEY_TRANSACTION_TYPE "transaction_type"
#define GRDM_JSON_KEY_BALANCE_DERIVATION_TYPE "balance_derivation_type"
#define GRDM_JSON_KEY_CROSS_GROUP_TYPE "cross_group_type"
#define GRDM_JSON_KEY_TX_RUNNING_HASH "tx_running_hash"
#define GRDM_JSON_KEY_BODY_BYTES "body_bytes"

// --- timestamp --------------------------------------------------------------------------
#define GRDM_JSON_KEY_SECONDS "seconds"
#define GRDM_JSON_KEY_NANOS "nanos"

// --- ledger anchor ----------------------------------------------------------------------
#define GRDM_JSON_KEY_TYPE "type"
#define GRDM_JSON_KEY_ID "id"
#define GRDM_JSON_KEY_HIERO_TRANSACTION_ID "hiero_transaction_id"
#define GRDM_JSON_KEY_TRANSACTION_VALID_START "transaction_valid_start"
#define GRDM_JSON_KEY_ACCOUNT_ID "account_id"
#define GRDM_JSON_KEY_SHARD_NUM "shard_num"
#define GRDM_JSON_KEY_REALM_NUM "realm_num"
#define GRDM_JSON_KEY_ACCOUNT_NUM "account_num"

// --- transaction detail, one of the three by transaction_type ---------------------------
#define GRDM_JSON_KEY_TRANSFER "transfer"
#define GRDM_JSON_KEY_SENDER_PUBKEY "sender_pubkey"
#define GRDM_JSON_KEY_RECIPIENT_PUBKEY "recipient_pubkey"
#define GRDM_JSON_KEY_AMOUNT "amount"
#define GRDM_JSON_KEY_COIN_COMMUNITY_UUID "coin_community_uuid"

#define GRDM_JSON_KEY_REGISTER_ADDRESS "register_address"
#define GRDM_JSON_KEY_USER_PUBLIC_KEY "user_public_key"
#define GRDM_JSON_KEY_NAME_HASH "name_hash"
#define GRDM_JSON_KEY_ACCOUNT_PUBLIC_KEY "account_public_key"
#define GRDM_JSON_KEY_ADDRESS_TYPE "address_type"
#define GRDM_JSON_KEY_DERIVATION_INDEX "derivation_index"

#define GRDM_JSON_KEY_COMMUNITY_ROOT "community_root"
#define GRDM_JSON_KEY_PUBLIC_KEY "public_key"
#define GRDM_JSON_KEY_GMW_PUBLIC_KEY "gmw_public_key"
#define GRDM_JSON_KEY_AUF_PUBLIC_KEY "auf_public_key"

// --- transaction context, one of the three by transaction_type --------------------------
#define GRDM_JSON_KEY_TARGET_DATE "target_date"
#define GRDM_JSON_KEY_TIMEOUT_DURATION "timeout_duration"
#define GRDM_JSON_KEY_PREVIOUS_TX "previous_tx"

// --- arrays -----------------------------------------------------------------------------
#define GRDM_JSON_KEY_ACCOUNT_BALANCES "account_balances"
#define GRDM_JSON_KEY_PUBKEY "pubkey"
#define GRDM_JSON_KEY_BALANCE "balance"
#define GRDM_JSON_KEY_COMMUNITY_UUID "community_uuid"

#define GRDM_JSON_KEY_ENCRYPTED_MEMOS "encrypted_memos"
#define GRDM_JSON_KEY_MEMO "memo"

#define GRDM_JSON_KEY_SIGNATURE_PAIRS "signature_pairs"
#define GRDM_JSON_KEY_SIGNATURE "signature"

// --- cross group ------------------------------------------------------------------------
#define GRDM_JSON_KEY_TX_PAIRING_COMMUNITY_UUID "tx_pairing_community_uuid"
#define GRDM_JSON_KEY_PAIRING_LEDGER_ANCHOR "pairing_ledger_anchor"

#endif // GRADIDO_BLOCKCHAIN_CORE_SRC_MAPPING_COMPLETE_TRANSACTION_JSON_H
