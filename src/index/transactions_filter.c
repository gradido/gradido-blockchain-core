#include "gradido_blockchain_core/index/transactions_filter.h"

#include <stdlib.h>
#include <string.h>

/**
 * A filter carries its own bytes. Everything here is a copy into it or a look at what is
 * already there -- no allocation but the one a host asks for, and nothing that outlives the
 * filter.
 */

/** Whether every byte of @p bytes is zero, which is how this filter spells "not set". */
static bool all_zero(const uint8_t *bytes, uint32_t size) {
  for (uint32_t i = 0; i < size; ++i) {
    if (bytes[i]) { return false; }
  }
  return true;
}

void grdx_transactions_filter_init(grdx_transactions_filter *filter) {
  if (!filter) { return; }
  memset(filter, 0, sizeof(*filter));
}

grdx_transactions_filter *grdx_transactions_filter_create(void) {
  grdx_transactions_filter *filter =
      (grdx_transactions_filter *)malloc(sizeof(grdx_transactions_filter));
  if (filter) { memset(filter, 0, sizeof(*filter)); }
  return filter;
}

void grdx_transactions_filter_free(grdx_transactions_filter *filter) {
  free(filter);
}

arnm_result grdx_transactions_filter_set_address(
    grdx_transactions_filter *filter, const uint8_t *public_key, grdx_address_role role
) {
  if (!filter) { return ARNM_ERROR_NULL_POINTER; }
  if ((uint32_t)role > (uint32_t)GRDX_ADDRESS_ROLE_BALANCE) { return ARNM_ERROR_INVALID_ENUM_TYPE; }
  if (GRDX_ADDRESS_ROLE_NONE == role) {
    // no role, no address: the key would never be read, so it does not stay behind either
    memset(filter->public_key, 0, SIGN_PUBLIC_KEY_SIZE);
    filter->role = GRDX_ADDRESS_ROLE_NONE;
    return ARNM_SUCCESS;
  }
  if (!public_key) { return ARNM_ERROR_NULL_POINTER; }
  memcpy(filter->public_key, public_key, SIGN_PUBLIC_KEY_SIZE);
  filter->role = role;
  return ARNM_SUCCESS;
}

arnm_result grdx_transactions_filter_set_transaction_type(
    grdx_transactions_filter *filter, grdt_transaction type
) {
  if (!filter) { return ARNM_ERROR_NULL_POINTER; }
  if ((uint32_t)type >= (uint32_t)GRDT_TRANSACTION_COUNT) { return ARNM_ERROR_INVALID_ENUM_TYPE; }
  filter->transaction_type = type;
  return ARNM_SUCCESS;
}

arnm_result grdx_transactions_filter_set_coin_community(
    grdx_transactions_filter *filter, const uint8_t *uuid
) {
  if (!filter) { return ARNM_ERROR_NULL_POINTER; }
  if (!uuid) {
    memset(filter->coin_community_uuid, 0, ARNM_UUID_BINARY_SIZE);
    return ARNM_SUCCESS;
  }
  // the nil uuid names no community, and inside the filter it already means "any"
  if (all_zero(uuid, ARNM_UUID_BINARY_SIZE)) { return ARNM_ERROR_INVALID_PARAM; }
  memcpy(filter->coin_community_uuid, uuid, ARNM_UUID_BINARY_SIZE);
  return ARNM_SUCCESS;
}

arnm_result grdx_transactions_filter_set_tx_range(
    grdx_transactions_filter *filter, uint64_t min_tx_nr, uint64_t max_tx_nr
) {
  if (!filter) { return ARNM_ERROR_NULL_POINTER; }
  filter->min_tx_nr = min_tx_nr;
  filter->max_tx_nr = max_tx_nr;
  return ARNM_SUCCESS;
}

arnm_result grdx_transactions_filter_set_date_range(
    grdx_transactions_filter *filter, int64_t from_seconds, int64_t to_seconds
) {
  if (!filter) { return ARNM_ERROR_NULL_POINTER; }
  filter->from_seconds = from_seconds;
  filter->to_seconds = to_seconds;
  return ARNM_SUCCESS;
}

const uint8_t *grdx_transactions_filter_address(const grdx_transactions_filter *filter) {
  if (!filter || GRDX_ADDRESS_ROLE_NONE == filter->role) { return NULL; }
  return filter->public_key;
}

const uint8_t *grdx_transactions_filter_coin_community(const grdx_transactions_filter *filter) {
  if (!filter || all_zero(filter->coin_community_uuid, ARNM_UUID_BINARY_SIZE)) { return NULL; }
  return filter->coin_community_uuid;
}
