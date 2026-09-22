#ifndef GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_FFI_H
#define GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_FFI_H

#include "arnm/converter.h"
#include "arnm/memory.h"
#include "gradido_blockchain_core/blockchain/store.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup grdb_chain_ffi grdb_chain_ffi
 * @ingroup blockchain
 * @brief A chain somebody else holds: the host is asked for the bytes of a transaction and this
 *        turns them into one.
 *
 * For a host that already keeps the chain -- a database behind a native module, a node's block
 * files, anything that can find a row by number. It implements three small functions, and
 * everything else about the chain stays in C: the filters, the counts, the pages, the address
 * register, and in time the validation.
 *
 * ### Bytes cross the boundary, never a struct
 *
 * The host hands over the serialized `ConfirmedTransaction` and nothing else.
 * @ref grdr_complete_transaction has two anonymous unions, an embedded allocator and padding
 * that follows the platform; rebuilding that layout in another language is work that falls due
 * again on every field added, and falls due silently when someone forgets. A length and a
 * pointer do not have versions. What the host has to be able to do is find a row and return its
 * bytes, which in most languages is a handful of lines that stay correct.
 *
 * ### Who owns what
 *
 * The **bytes** are the host's, for as long as one call takes.
 * @ref grdb_chain_ffi_host::get points at them, the adapter decodes them, and
 * @ref grdb_chain_ffi_host::release is called before the fetch returns -- so a host answering
 * from a temporary buffer, a copied row or a garbage collected array can say when it is done.
 * A host whose bytes live on their own leaves @c release NULL.
 *
 * The **transaction** is the adapter's. It keeps one place to decode into and hands back its
 * address, so the pointer from a fetch is good until the next call on this store, exactly as
 * @ref grdb_chain_store says. Asking twice for the same number costs one decode, not two: the
 * second finds it already there.
 *
 * Writing is bytes only: the store offers @ref grdb_chain_store::append_serialized, handed to
 * @ref grdb_chain_ffi_host::put, and leaves @ref grdb_chain_store::append NULL -- a host that
 * keeps rows has nowhere to put a C struct.
 *
 * @note Not thread safe, and neither is what it calls: one thread at a time per adapter.
 *
 * @whisper A question passed over the border, and an answer that needs no shared language
 *
 * @{
 */

/** @brief The host found the transaction and the pointer is good. */
#define GRDB_FFI_OK 0
/** @brief The host holds no transaction with that number. Not an error; an answer. */
#define GRDB_FFI_NOT_FOUND 1

/** @brief Bytes the decoder works in when the options name no other size. */
#define GRDB_FFI_SCRATCH_DEFAULT (256u * 1024u)

/**
 * @brief What the host implements. @c get is required; the other two may be NULL.
 *
 * Every call gets @c user_data back, and nothing else is shared.
 */
typedef struct grdb_chain_ffi_host {
  /** Passed to every call below; whatever the host needs to find its rows. */
  void *user_data;

  /**
   * @brief Find transaction @p tx_nr and point at its serialized bytes. Required.
   *
   * @param[in,out] user_data As given in this struct.
   * @param[in]     tx_nr     The number to find.
   * @param[out]    data      Receives the start of the bytes; valid until @c release is called,
   *                          or until the fetch returns when there is no @c release.
   * @param[out]    size      Receives how many bytes.
   * @retval GRDB_FFI_OK        Found; @p data and @p size are set.
   * @retval GRDB_FFI_NOT_FOUND No such transaction.
   * @return Any other value when the lookup itself failed; the chain answers
   *         @c ARNM_ERROR_DECODE_FAILED and the value is not otherwise read.
   */
  int (*get)(void *user_data, uint64_t tx_nr, const uint8_t **data, uint32_t *size);

  /**
   * @brief Done with the bytes a @c get handed over. May be NULL.
   *
   * Called exactly once for every @c get that answered @ref GRDB_FFI_OK, before the fetch
   * returns, whether the decode succeeded or not.
   */
  void (*release)(void *user_data, const uint8_t *data, uint32_t size);

  /**
   * @brief Keep @p data as transaction @p tx_nr. NULL makes the chain read only.
   *
   * @retval GRDB_FFI_OK Kept; the bytes need not outlive the call.
   * @return Any other value when the write failed; the chain answers @c ARNM_ERROR_ENCODE_FAILED.
   */
  int (*put)(void *user_data, uint64_t tx_nr, const uint8_t *data, uint32_t size);
} grdb_chain_ffi_host;

/** @brief Shape of an adapter. `{0}` is the default. */
typedef struct grdb_chain_ffi_options {
  /** Scratch for one decode; 0 takes @ref GRDB_FFI_SCRATCH_DEFAULT. Rounded up to a
   *  multiple of 8, which is what the decoder wants of it. */
  uint32_t scratch_bytes;
} grdb_chain_ffi_options;

/**
 * @brief The adapter between a host's bytes and a chain. Zeroed is not ready;
 *        @ref grdb_chain_ffi_init() makes it so.
 */
typedef struct grdb_chain_ffi {
  grdb_chain_ffi_host host;       /**< What the host implements. Copied at init. */
  arnm *source;                   /**< Where the scratch comes from. */
  grdr_complete_transaction held; /**< The one place decoding happens; the fetch's answer. */
  uint64_t held_tx_nr;            /**< Which number is in it; 0 for none. */
  uint8_t *scratch;               /**< Where one decode works. */
  uint32_t scratch_size;          /**< Its size, a multiple of 8. */
  uint8_t community_uuid[ARNM_UUID_BINARY_SIZE]; /**< The chain's community, given at init. */
  bool ready;                                    /**< Init ran and release did not. */
} grdb_chain_ffi;

/**
 * @brief Prepare an adapter over @p host.
 *
 * @param[out]    adapter        Adapter to prepare; not NULL. Overwritten whole.
 * @param[in]     host           What the host implements; not NULL, and @c get not NULL.
 *                               Copied, so the struct need not outlive this call -- but what
 *                               @c user_data points at has to.
 * @param[in]     options        Shape; NULL gives the defaults.
 * @param[in]     community_uuid 16 bytes the decoded transactions carry; not NULL. Copied.
 * @param[in,out] source         Allocator for the scratch; NULL for the host allocator.
 * @retval ARNM_SUCCESS             Ready.
 * @retval ARNM_ERROR_NULL_POINTER        An argument is NULL, or @c host->get is.
 * @retval ARNM_ERROR_ARITHMETIC_OVERFLOW @c scratch_bytes is above `UINT32_MAX - 7`, where the
 *                                        rounding to a multiple of 8 would wrap. @p adapter is
 *                                        left untouched.
 * @retval ARNM_ERROR_OUT_OF_MEMORY       No room for the scratch.
 * @whisper A desk set up at the border crossing
 */
arnm_result grdb_chain_ffi_init(
    grdb_chain_ffi *adapter,
    const grdb_chain_ffi_host *host,
    const grdb_chain_ffi_options *options,
    const uint8_t community_uuid[ARNM_UUID_BINARY_SIZE],
    arnm *source
);

/**
 * @brief Give the scratch and the held transaction back, and leave the adapter as init found
 *        it. The host is not called.
 * @param[in,out] adapter Adapter to release; NULL is a no-op.
 */
void grdb_chain_ffi_release(grdb_chain_ffi *adapter);

/**
 * @brief The @ref grdb_chain_store that reads and writes through @p adapter.
 *
 * A plain value pointing at @p adapter, which therefore has to outlive every chain it is given
 * to. The store is writable only where the host gave a @c put.
 *
 * @param[in] adapter Adapter to wrap; may be NULL.
 */
grdb_chain_store grdb_chain_ffi_as_store(grdb_chain_ffi *adapter);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_BLOCKCHAIN_FFI_H
