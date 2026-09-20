#include "bench_chain_data.h"

#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/runtime/complete_transaction.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"

#include <stdalign.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint8_t bench_default_community_uuid[BENCH_UUID_SIZE] = {0xe7, 0x0d, 0xa3, 0x3e, 0x59, 0x76,
                                                               0x47, 0x67, 0xba, 0xde, 0xaa, 0x4e,
                                                               0x4f, 0xa1, 0xc0, 0x1a};

#define DECODE_BUFFER_SIZE (256u * 1024u)
#define MAX_TX_SIZE 65535u

const char *bench_chain_data_path(int argc, char **argv) {
  if (argc > 1 && argv[1] && argv[1][0]) { return argv[1]; }
  const char *env = getenv("GRD_BENCH_CHAIN_DATA");
  return (env && env[0]) ? env : NULL;
}

static arnm_result data_reserve(bench_chain_data *data, uint32_t count) {
  if (count <= data->capacity) { return ARNM_SUCCESS; }
  uint32_t capacity = data->capacity ? data->capacity : 1024u;
  while (capacity < count) { capacity *= 2u; }
  bench_tx_record *records =
      (bench_tx_record *)realloc(data->records, (size_t)capacity * sizeof(bench_tx_record));
  if (!records) { return ARNM_ERROR_OUT_OF_MEMORY; }
  data->records = records;
  data->capacity = capacity;
  return ARNM_SUCCESS;
}

static void data_init(bench_chain_data *data, const uint8_t own_community[BENCH_UUID_SIZE]) {
  memset(data, 0, sizeof(*data));
  memcpy(data->communities[0], own_community, BENCH_UUID_SIZE);
  data->community_count = 1;
}

/** 0 for the own community, else a 1 based local id; 0 as well when the table is full. */
static uint8_t community_local_id(bench_chain_data *data, const uint8_t uuid[BENCH_UUID_SIZE]) {
  for (uint32_t i = 0; i < data->community_count; ++i) {
    if (0 == memcmp(data->communities[i], uuid, BENCH_UUID_SIZE)) { return (uint8_t)i; }
  }
  if (data->community_count == BENCH_COMMUNITY_MAX) { return 0u; }
  memcpy(data->communities[data->community_count], uuid, BENCH_UUID_SIZE);
  return (uint8_t)data->community_count++;
}

static bool is_zero_key(const uint8_t *key) {
  for (unsigned i = 0; i < PROTO_KEY_SIZE; ++i) {
    if (key[i]) { return false; }
  }
  return true;
}

/** Adds @p roles to @p key's entry, creating it; false when the record is full. */
static bool record_touch(bench_tx_record *record, const uint8_t *key, uint8_t roles) {
  if (!key || is_zero_key(key)) { return true; }
  for (uint8_t i = 0; i < record->address_count; ++i) {
    if (0 == memcmp(record->addresses[i].key, key, PROTO_KEY_SIZE)) {
      record->addresses[i].roles |= roles;
      return true;
    }
  }
  if (record->address_count == BENCH_TX_ADDRESS_MAX) { return false; }
  bench_tx_address *address = &record->addresses[record->address_count++];
  memcpy(address->key, key, PROTO_KEY_SIZE);
  address->roles = roles;
  return true;
}

static void record_from_complete(
    bench_chain_data *data, bench_tx_record *record, const grdr_complete_transaction *tx
) {
  memset(record, 0, sizeof(*record));
  record->tx_nr = tx->tx_nr;
  record->confirmed_seconds = tx->confirmed_at.seconds;
  record->type = (uint8_t)tx->transaction_type;
  bool fits = true;

  for (size_t i = 0; i < tx->signature_pairs_count; ++i) {
    fits &= record_touch(record, tx->signature_pairs[i].public_key, BENCH_ADDRESS_SIGNER);
  }
  for (size_t i = 0; i < tx->account_balances_count; ++i) {
    const grdw_account_balance *balance = &tx->account_balances[i];
    fits &= record_touch(record, balance->pubkey, BENCH_ADDRESS_BALANCE);
    uint8_t coin = community_local_id(data, balance->community_uuid);
    if (coin) {
      bool seen = false;
      for (uint8_t j = 0; j < record->foreign_coin_count; ++j) {
        seen |= record->foreign_coin[j] == coin;
      }
      if (!seen && record->foreign_coin_count < BENCH_TX_FOREIGN_COIN_MAX) {
        record->foreign_coin[record->foreign_coin_count++] = coin;
      }
    }
  }
  switch (tx->transaction_type) {
  case GRDT_TRANSACTION_CREATION:
  case GRDT_TRANSACTION_TRANSFER:
  case GRDT_TRANSACTION_DEFERRED_TRANSFER:
  case GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER:
    fits &= record_touch(record, tx->transfer.sender_pubkey, 0);
    fits &= record_touch(record, tx->transfer.recipient_pubkey, 0);
    break;
  case GRDT_TRANSACTION_REGISTER_ADDRESS:
    fits &= record_touch(record, tx->register_address.user_public_key, 0);
    fits &= record_touch(record, tx->register_address.account_public_key, 0);
    break;
  case GRDT_TRANSACTION_COMMUNITY_ROOT:
    fits &= record_touch(record, tx->community_root.public_key, 0);
    fits &= record_touch(record, tx->community_root.gmw_public_key, 0);
    fits &= record_touch(record, tx->community_root.auf_public_key, 0);
    break;
  default:
    break;
  }
  // an address with no role so far was named in the body only
  for (uint8_t i = 0; i < record->address_count; ++i) {
    if (!record->addresses[i].roles) { record->addresses[i].roles = BENCH_ADDRESS_OTHER; }
  }
  if (!fits) { data->address_overflows++; }
}

arnm_result bench_chain_data_load(
    bench_chain_data *data, const char *path, const uint8_t community_uuid[BENCH_UUID_SIZE]
) {
  data_init(data, community_uuid);
  FILE *file = path ? fopen(path, "rb") : NULL;
  if (!file) { return ARNM_ERROR_INVALID_PARAM; }

  static alignas(8) uint8_t decode_buffer[DECODE_BUFFER_SIZE];
  static uint8_t tx_buffer[MAX_TX_SIZE];
  grdr_complete_transaction tx;
  grdr_complete_transaction_init(&tx);

  uint8_t size_bytes[2];
  while (2u == fread(size_bytes, 1, 2, file)) {
    uint16_t size = (uint16_t)(size_bytes[0] | (size_bytes[1] << 8));
    if (!size) { continue; }
    if (size != fread(tx_buffer, 1, size, file)) { break; }
    data->input_bytes += 2u + size;
    arnm_result result = grdr_complete_transaction_init_from_protobuf(
        &tx, tx_buffer, size, community_uuid, decode_buffer, DECODE_BUFFER_SIZE
    );
    if (ARNM_SUCCESS != result) {
      data->decode_failures++;
      continue;
    }
    result = data_reserve(data, data->count + 1u);
    if (ARNM_SUCCESS != result) {
      grdr_complete_transaction_release(&tx);
      fclose(file);
      return result;
    }
    record_from_complete(data, &data->records[data->count++], &tx);
    grdr_complete_transaction_release(&tx);
  }
  fclose(file);
  return data->count ? ARNM_SUCCESS : ARNM_ERROR_INVALID_PARAM;
}

arnm_result bench_chain_data_synthetic(
    bench_chain_data *data, uint32_t tx_count, uint32_t key_count, uint64_t seed
) {
  static const uint8_t synthetic_community[BENCH_UUID_SIZE] = {0x5e, 0x5e, 0x5e, 0x5e};
  data_init(data, synthetic_community);
  if (!tx_count || !key_count) { return ARNM_ERROR_INVALID_PARAM; }
  arnm_result result = data_reserve(data, tx_count);
  if (ARNM_SUCCESS != result) { return result; }

  uint8_t foreign[BENCH_UUID_SIZE] = {0xf0, 0x4e, 0x19, 0x40};
  uint8_t foreign_id = community_local_id(data, foreign);

  uint8_t *keys = (uint8_t *)malloc((size_t)key_count * PROTO_KEY_SIZE);
  if (!keys) { return ARNM_ERROR_OUT_OF_MEMORY; }
  uint64_t state = seed;
  for (uint32_t k = 0; k < key_count; ++k) {
    bench_random_key(&state, keys + (size_t)k * PROTO_KEY_SIZE);
  }

  int64_t seconds = 1700000000;
  for (uint32_t i = 0; i < tx_count; ++i) {
    bench_tx_record *record = &data->records[data->count++];
    memset(record, 0, sizeof(*record));
    record->tx_nr = (uint64_t)i + 1u;
    seconds += (int64_t)(bench_random_next(&state) % 120u);
    record->confirmed_seconds = seconds;

    uint64_t roll = bench_random_next(&state) % 100u;
    record->type = (uint8_t)(roll < 70u   ? GRDT_TRANSACTION_TRANSFER
                             : roll < 85u ? GRDT_TRANSACTION_CREATION
                             : roll < 93u ? GRDT_TRANSACTION_REGISTER_ADDRESS
                             : roll < 97u ? GRDT_TRANSACTION_DEFERRED_TRANSFER
                                          : GRDT_TRANSACTION_REDEEM_DEFERRED_TRANSFER);
    // squaring a uniform draw skews it towards 0: low key ids are the busy addresses
    for (uint8_t p = 0; p < 2u; ++p) {
      double u = (double)(bench_random_next(&state) >> 11) / 9007199254740992.0;
      uint32_t key = (uint32_t)(u * u * key_count);
      if (key >= key_count) { key = key_count - 1u; }
      uint8_t roles = BENCH_ADDRESS_OTHER;
      if (GRDT_TRANSACTION_REGISTER_ADDRESS != record->type) {
        roles = p ? BENCH_ADDRESS_BALANCE : (uint8_t)(BENCH_ADDRESS_SIGNER | BENCH_ADDRESS_BALANCE);
      } else if (!p) {
        roles = BENCH_ADDRESS_SIGNER;
      }
      record_touch(record, keys + (size_t)key * PROTO_KEY_SIZE, roles);
    }
    if (0u == bench_random_next(&state) % 200u) {
      record->foreign_coin[record->foreign_coin_count++] = foreign_id;
    }
  }
  free(keys);
  return ARNM_SUCCESS;
}

void bench_chain_data_free(bench_chain_data *data) {
  free(data->records);
  memset(data, 0, sizeof(*data));
}
