#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_WRITER_C
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_WRITER_C

/**
 * @file
 * @brief Included, not compiled.
 *
 * This is a translation unit fragment: the two JSON mappings beside it write
 * `#include "json_writer.c"` and get everything here as their own, which is why every function
 * is static and why both build files leave the file out of their source lists. Compiling it on
 * its own would produce an object of unreachable statics.
 *
 * It is a `.c` rather than a header because of what it needs: yyjson. A header in
 * `include/gradido_blockchain_core` is on the include path of everything that touches this
 * library, and one that pulls yyjson in puts a third party parser there with it. Declaring the
 * value builders in a header and defining them here as ordinary functions would avoid that too,
 * and was measured at about five percent on a typical transaction -- the builders are small
 * enough that a call is a real share of them. Included whole, they inline as before and no
 * header names yyjson at all.
 *
 * @whisper One body of work, carried into both houses that need it
 */

#include "gradido_blockchain_core/const.h"
#include "gradido_blockchain_core/data/wire/hiero.h"
#include "gradido_blockchain_core/types/ledger_anchor.h"
#include "gradido_blockchain_core/types/memo_key.h"

#include <string.h>

#include "gradido_blockchain_core/data/timestamp.h"
#include "gradido_blockchain_core/data/types.h"
#include "gradido_blockchain_core/data/unit.h"
#include "gradido_blockchain_core/data/wire/basic_types.h"
#include "gradido_blockchain_core/data/wire/ledger_anchor.h"
#include "gradido_blockchain_core/mapping/json_format.h"
#include "hostmem/converter.h"
#include "hostmem/memory_block.h"
#include "hostmem/multi_arena.h"
#include "hostmem/result.h"

#include "yyjson.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @defgroup grdm_json_writer grdm_json_writer
 *  @ingroup mapping
 *  @brief The half of a JSON render that does not depend on which structure is being rendered
 *
 * Support for the two JSON mappings beside it rather than something to render with directly:
 * a caller who wants text calls grdm_complete_transaction_to_json() or one of the three in
 * @ref grdm_json_from_wire. What lives here is what those two would otherwise have written
 * twice.
 *
 * Both mappings in this folder run the same course: open a document on the caller's work chain,
 * turn binary fields into text a reader knows them by, and hand the finished text to the
 * caller's result chain. Only the walk over the structure in between differs. The course
 * itself lives here, because it was written once for the runtime mapping and would otherwise
 * have been written a second time for the wire one -- along with every judgement in it, such
 * as which allocator the writer draws from and why the text is copied rather than written in
 * place.
 *
 * What a caller of this header writes is the walk, and nothing else:
 *
 * @code
 * grdm_json_writer writer;
 * yyjson_mut_val *root = NULL;
 * hostmem_result opened = grdm_json_writer_begin(&writer, &root, work);
 * if (HOSTMEM_SUCCESS != opened) { return opened; }
 * // ... fill root, answering a false or NULL with grdm_json_failure(&writer) ...
 * return grdm_json_writer_finish(json, &writer, format, result);
 * @endcode
 *
 * ### The text forms
 *
 * Fixed here rather than per mapping, so that two views of the same transaction never disagree
 * about what a field means:
 *
 * - **Binary** -- keys, hashes, signatures, memo payloads -- is lowercase hex, two characters
 *   per byte, no separator and no prefix.
 * - **Community uuids** are the canonical 8-4-4-4-12 form.
 * - **Timestamps** are `seconds.nanoseconds`, the nanoseconds always nine digits. Fields
 *   carrying whole seconds stay JSON numbers.
 * - **Amounts** are decimal strings with four fractional digits, the form
 *   grdd_unit_from_string() reads back. Never JSON numbers: a fixed-point value of scale 10^4
 *   passed through a double comes back changed.
 * - **Enums** are their enumerator names, from the `grdt_*_to_string()` the rest of the
 *   project prints them with.
 *
 * @note Every string these builders produce comes from a closed alphabet and is marked as
 *       needing no escaping. See grdm_json_noesc() for what that promises and who keeps it.
 * @whisper One course, walked twice, by two different paths through the same data
 *  @{
 */

/** @brief Characters grdd_unit_to_string() can write at precision 4, terminator included.
 *
 *  The widest value is INT64_MIN, twenty digits, and the sign and the decimal point are two
 *  more: `-922337203685477.5808` is 21 characters. One byte for the terminator closes it.
 *  A compile time bound, so the buffer is sized once and never measured again.
 */
#define GRDM_JSON_UNIT_CAPACITY 22

/**
 * @brief A document being built, and where its bytes come from.
 *
 * grdm_json_writer_begin() writes every field and reads none, so uninitialized storage is a
 * valid input.
 */
typedef struct grdm_json_writer {
  //! The document. Its allocator is @p work, so nothing here reaches malloc.
  yyjson_mut_doc *doc;
  //! Chain the document, the field text and the writer's output buffer are drawn from.
  hostmem_multi_arena *work;
  /**
   * @brief The first refusal that was not the chain running dry, or HOSTMEM_SUCCESS.
   *
   * The value builders answer with NULL whatever went wrong, which leaves two causes that read
   * alike at the call site. A value that refused to be written as text records itself here on
   * the way out, so the code the caller finally sees names the field's own limit rather than
   * blaming the allocator for it.
   */
  hostmem_result failure;
} grdm_json_writer;

// ****************** the course ************************************************************

/**
 * @brief Open a document on @p work and give back its root object.
 *
 * @param[out]    writer Writer to prepare; not NULL. Need not be zeroed.
 * @param[out]    root   Receives the root object, already set as the document's root; not NULL.
 * @param[in,out] work   Chain every byte of the rendering is drawn from; not NULL.
 * @retval HOSTMEM_SUCCESS             Document open, @p root ready to be filled.
 * @retval HOSTMEM_ERROR_NULL_POINTER  An argument is NULL.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY @p work could not open another arena.
 * @whisper A basin is opened, and the stream is pointed into it
 */
static hostmem_result grdm_json_writer_begin(
    grdm_json_writer *writer, yyjson_mut_val **root, hostmem_multi_arena *work
);

/**
 * @brief Write the document out and place the finished text in @p result.
 *
 * The writer draws its output buffer from the work chain and the text is copied across, rather
 * than written into @p result directly. The reason is that yyjson grows that buffer as it goes
 * and never shrinks it to the length it ended up at: writing in place would leave the result
 * chain holding the writer's estimate and every superseded buffer, measured at 2.5 to 5.5
 * times the text. The copy costs about two percent of a render and leaves @p result holding
 * the text and nothing else.
 *
 * @param[out]    json   Receives the text: @c data points into @p result, @c size counts the
 *                       characters. One further byte is written after them, a terminator, so
 *                       reclaiming the block by hand passes @c size + 1. Untouched on failure.
 * @param[in]     writer Writer holding the finished document; not NULL.
 * @param[in]     format Compact or indented.
 * @param[in,out] result Chain the text is placed in; not NULL. May be the work chain, at the
 *                       price of the scratch staying beside the text until the reset.
 * @retval HOSTMEM_SUCCESS                   @p json holds the text.
 * @retval HOSTMEM_ERROR_NULL_POINTER        An argument is NULL.
 * @retval HOSTMEM_ERROR_ARITHMETIC_OVERFLOW The text is longer than the @c uint32_t hostmem
 *                                           measures an allocation in.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY       A chain could not open another arena.
 * @whisper What was gathered flows out as one continuous line
 */
static hostmem_result grdm_json_writer_finish(
    hostmem_memory_block *json,
    const grdm_json_writer *writer,
    grdm_json_format format,
    hostmem_multi_arena *result
);

/**
 * @brief The code a failed render reports.
 *
 * A recorded refusal if there was one; memory exhaustion otherwise, which is the only other way
 * a value can fail to appear.
 */
static inline hostmem_result grdm_json_failure(const grdm_json_writer *writer) {
  return HOSTMEM_SUCCESS != writer->failure ? writer->failure : HOSTMEM_ERROR_OUT_OF_MEMORY;
}

// ****************** fields, turned into text **********************************************
//
// Each of these leaves its characters in the work chain and hands yyjson a view of them
// (yyjson_mut_strn does not copy). Both live in the same chain and are released together, so
// the second copy a copying variant would make would be paid for and never needed. Measured,
// that non-copying choice is worth 7 percent on a small transaction and 36 on a large one.

/**
 * @brief Hand yyjson a string it may write out without scanning it.
 *
 * Every string built here comes from a closed alphabet -- hex digits, the uuid separator,
 * digits with a sign and a decimal point -- so none of them can hold a quote, a backslash, a
 * control character or a byte that is not valid UTF-8. Saying so lets the writer memcpy the
 * string between its quotes instead of walking it character by character, which is worth 20
 * percent on a typical transaction and 47 on one carrying body bytes.
 *
 * The promise is the caller's to keep: mark nothing that a transaction's own bytes reach
 * unencoded. Binary fields go out as hex, which is what makes the promise hold.
 *
 * @note Object keys need no such call. yyjson_mut_obj_add_str() and its siblings mark the key
 *       themselves, with a check the compiler folds away for a literal.
 */
static inline yyjson_mut_val *grdm_json_noesc(yyjson_mut_val *val) {
  yyjson_mut_set_str_noesc(val, true);
  return val;
}

/**
 * @brief @p size bytes from the work chain, to write characters into.
 *
 * The seam between two ways of reporting failure. hostmem answers with a result code and fills
 * an out parameter; the value builders below answer with the value and mean failure by NULL.
 * Crossing that once, here, is what lets them all read alike -- and it puts the one cast from
 * bytes to characters in a place whose name says why it is a cast to characters.
 *
 * @return The buffer, or NULL when the chain could not open another arena. Not zeroed: it holds
 *         whatever the previous tenant left, and every caller writes before it reads.
 * @whisper Ground is set aside, and the characters settle into it
 */
static inline char *grdm_json_text_buffer(hostmem_multi_arena *work, uint32_t size) {
  uint8_t *buffer = NULL;
  if (HOSTMEM_SUCCESS != hostmem_multi_arena_alloc(&buffer, size, work)) { return NULL; }
  return (char *)buffer;
}

/** @brief Lowercase hex of @p size bytes, two characters each. NULL when @p size is 0. */
static inline yyjson_mut_val *grdm_json_hex(
    grdm_json_writer *writer, const uint8_t *data, uint32_t size
) {
  if (!data || !size) { return NULL; }
  // two characters per byte and a terminator, counted in the uint32_t hostmem measures an
  // allocation in -- a block past this bound would wrap into a buffer too small to write into
  if (size > (UINT32_MAX - 1) / 2) {
    writer->failure = HOSTMEM_ERROR_ARITHMETIC_OVERFLOW;
    return NULL;
  }
  char *buffer = grdm_json_text_buffer(writer->work, size * 2 + 1);
  if (!buffer) { return NULL; }
  const hostmem_memory_block block = {(uint8_t *)data, size};
  if (HOSTMEM_SUCCESS != hostmem_binary_to_hex(buffer, &block)) { return NULL; }
  return grdm_json_noesc(yyjson_mut_strn(writer->doc, buffer, (size_t)size * 2));
}

/** @brief The canonical 8-4-4-4-12 form of 16 bytes. */
static inline yyjson_mut_val *grdm_json_uuid(
    grdm_json_writer *writer, const uint8_t uuid[HOSTMEM_UUID_BINARY_SIZE]
) {
  if (!uuid) { return NULL; }
  char *buffer = grdm_json_text_buffer(writer->work, HOSTMEM_UUID_STRING_LENGTH + 1);
  if (!buffer) { return NULL; }
  hostmem_uuid_to_string(buffer, uuid);
  return grdm_json_noesc(yyjson_mut_strn(writer->doc, buffer, HOSTMEM_UUID_STRING_LENGTH));
}

/** @brief `seconds.nanoseconds`, nanoseconds always nine digits. NULL when nanos is out of
 *         range, which is the one input grdd_timestamp_to_string() refuses. */
static inline yyjson_mut_val *grdm_json_timestamp(
    grdm_json_writer *writer, const grdd_timestamp *timestamp
) {
  size_t size = grdd_timestamp_calculate_string_size(timestamp);
  if (!size) {
    writer->failure = HOSTMEM_ERROR_ENCODE_FAILED;
    return NULL;
  }
  char *buffer = grdm_json_text_buffer(writer->work, (uint32_t)size + 1);
  if (!buffer) { return NULL; }
  // the buffer is sized from the same figure this returns, so a short write says the value
  // changed underneath us rather than that the buffer was misjudged
  if (grdd_timestamp_to_string(buffer, size + 1, timestamp) != size) {
    writer->failure = HOSTMEM_ERROR_ENCODE_FAILED;
    return NULL;
  }
  return grdm_json_noesc(yyjson_mut_strn(writer->doc, buffer, size));
}

/** @brief Fixed-point GDD as a decimal string with four fractional digits.
 *
 *  Never a JSON number: the value is scaled by 10^4 and a double carries only 53 bits of
 *  mantissa, so amounts above 2^53 / 10^4 would come back changed from a reader that parses
 *  numbers as doubles -- which most of them do.
 */
static inline yyjson_mut_val *grdm_json_unit(grdm_json_writer *writer, grdd_unit value) {
  char *buffer = grdm_json_text_buffer(writer->work, GRDM_JSON_UNIT_CAPACITY);
  if (!buffer) { return NULL; }
  int written = grdd_unit_to_string(buffer, GRDM_JSON_UNIT_CAPACITY, value, 4);
  // negative is a refusal; a figure reaching the capacity is the "this is what I would have
  // needed" answer, which means nothing was written
  if (written <= 0 || written >= GRDM_JSON_UNIT_CAPACITY) {
    writer->failure = HOSTMEM_ERROR_ENCODE_FAILED;
    return NULL;
  }
  return grdm_json_noesc(yyjson_mut_strn(writer->doc, buffer, (size_t)written));
}

// ****************** wire leaves both mappings render **************************************
//
// These four structures reach the text unchanged whichever mapping is walking: a runtime
// transaction carries them by value, a wire one by the same types under different names. They
// are rendered here so that the two views cannot drift apart in what they call a signature or
// how they spell an anchor.

/**
 * @brief The anchor as `{ "type": ..., ... }`, the second member chosen by the type.
 *
 * The union follows the discriminator: a Hiero anchor carries a transaction id, the legacy and
 * node trigger anchors carry a number, and the unspecified anchor carries nothing worth
 * printing -- which is why it is written as the type alone rather than as a zero.
 */
static yyjson_mut_val *grdm_json_ledger_anchor(
    grdm_json_writer *writer, const grdw_ledger_anchor *anchor
);

/**
 * @brief Add an array of account balances under @p key.
 *
 * Each element becomes `{ "pubkey": ..., "balance": ..., "community_uuid": ... }`.
 *
 * The array is added even when @p count is 0, so a reader that walks it finds a list to walk in
 * every transaction rather than having to tell a missing member from an empty one. The same
 * holds for the two below.
 *
 * @return false when a value could not be built; grdm_json_failure() names the reason.
 */
static bool grdm_json_add_account_balances(
    grdm_json_writer *writer,
    yyjson_mut_val *obj,
    const char *key,
    const grdw_account_balance *balances,
    size_t count
);

/**
 * @brief Add an array of encrypted memos under @p key.
 *
 * Each element becomes `{ "type": ... }`, with a `"memo"` member of hex beside it when the
 * payload holds bytes. An empty memo is a memo still: the type is what it carries, and an empty
 * string would tell a reader to decode nothing rather than that there was nothing.
 *
 * @return false when a value could not be built; grdm_json_failure() names the reason.
 */
static bool grdm_json_add_encrypted_memos(
    grdm_json_writer *writer,
    yyjson_mut_val *obj,
    const char *key,
    const grdw_encrypted_memo *memos,
    size_t count
);

/**
 * @brief Add an array of signature pairs under @p key.
 *
 * Each element becomes `{ "public_key": ..., "signature": ... }`.
 *
 * @return false when a value could not be built; grdm_json_failure() names the reason.
 */
static bool grdm_json_add_signature_pairs(
    grdm_json_writer *writer,
    yyjson_mut_val *obj,
    const char *key,
    const grdw_signature_pair *pairs,
    size_t count
);

/** @} */

// ****************** the work chain, seen through yyjson's allocator ***********************
//
// yyjson asks for memory through three function pointers and a context. The context here is a
// hostmem_multi_arena, so every byte a render needs -- the document, the value pool, the
// writer's output buffer -- is drawn from the chain the caller opened, and malloc is never
// reached. A bump chain hands memory back in one motion rather than block by block, which is
// what shapes the three below.

static void *arena_malloc(void *ctx, size_t size) {
  hostmem_multi_arena *chain = (hostmem_multi_arena *)ctx;
  if (size > UINT32_MAX) { return NULL; }
  // NULL is how this interface says "no memory", so a zero sized request cannot answer with it
  // and asks for one byte instead -- which the arena rounds up to its alignment anyway
  uint8_t *buffer = NULL;
  if (HOSTMEM_SUCCESS != hostmem_multi_arena_alloc(&buffer, size ? (uint32_t)size : 1, chain)) {
    return NULL;
  }
  return buffer;
}

static void *arena_realloc(void *ctx, void *ptr, size_t old_size, size_t size) {
  if (!ptr) { return arena_malloc(ctx, size); }
  // the block already reaches that far; a bump allocator cannot give the difference back, and
  // the reservation stays what it was
  if (size <= old_size) { return ptr; }
  void *grown = arena_malloc(ctx, size);
  if (!grown) { return NULL; }
  memcpy(grown, ptr, old_size);
  return grown;
}

static void arena_free(void *ctx, void *ptr) {
  // Deliberately empty. hostmem_multi_arena_free() reclaims a block only while it is the tail
  // of its own arena, and it needs the size to do it -- a size this interface does not carry.
  // So nothing is attempted: the chain comes back whole at the caller's
  // hostmem_multi_arena_reset(), which is what the public headers promise.
  (void)ctx;
  (void)ptr;
}

// ****************** the course ************************************************************

static hostmem_result grdm_json_writer_begin(
    grdm_json_writer *writer, yyjson_mut_val **root, hostmem_multi_arena *work
) {
  if (!writer || !root || !work) { return HOSTMEM_ERROR_NULL_POINTER; }

  // by value into the document, so this local going out of scope is none of yyjson's business
  const yyjson_alc alc = {arena_malloc, arena_realloc, arena_free, work};

  yyjson_mut_doc *doc = yyjson_mut_doc_new(&alc);
  if (!doc) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  yyjson_mut_val *object = yyjson_mut_obj(doc);
  if (!object) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  yyjson_mut_doc_set_root(doc, object);

  writer->doc = doc;
  writer->work = work;
  writer->failure = HOSTMEM_SUCCESS;
  *root = object;
  return HOSTMEM_SUCCESS;
}

static hostmem_result grdm_json_writer_finish(
    hostmem_memory_block *json,
    const grdm_json_writer *writer,
    grdm_json_format format,
    hostmem_multi_arena *result
) {
  if (!json || !writer || !result) { return HOSTMEM_ERROR_NULL_POINTER; }

  // the same allocator the document was opened on, so the output buffer is scratch like
  // everything else and never touches the chain the caller means to keep
  const yyjson_alc alc = {arena_malloc, arena_realloc, arena_free, writer->work};
  const yyjson_write_flag flags =
      (GRDM_JSON_PRETTY == format) ? YYJSON_WRITE_PRETTY : YYJSON_WRITE_NOFLAG;

  size_t length = 0;
  char *text = yyjson_mut_write_opts(writer->doc, flags, &alc, &length, NULL);
  if (!text) { return HOSTMEM_ERROR_OUT_OF_MEMORY; }
  // one more byte for the terminator, and hostmem measures an allocation in uint32_t
  if (length >= UINT32_MAX) { return HOSTMEM_ERROR_ARITHMETIC_OVERFLOW; }

  // the single allocation the result chain ever sees -- see the header for why it is a copy
  uint8_t *out = NULL;
  hostmem_result cloned =
      hostmem_multi_arena_clone(&out, (const uint8_t *)text, (uint32_t)length + 1, result);
  if (HOSTMEM_SUCCESS != cloned) { return cloned; }

  // size counts the characters; the terminator sits after them, the way the rest of the
  // project's _to_string() functions count
  json->data = out;
  json->size = (uint32_t)length;
  return HOSTMEM_SUCCESS;
}

// ****************** wire leaves both mappings render **************************************

static yyjson_mut_val *hiero_transaction_id(
    grdm_json_writer *writer, const grdw_hiero_transaction_id *hiero
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  if (!obj) { return NULL; }

  yyjson_mut_val *valid_start = grdm_json_timestamp(writer, &hiero->transactionValidStart);
  yyjson_mut_val *account = yyjson_mut_obj(doc);
  if (!valid_start || !account) { return NULL; }

  bool ok = yyjson_mut_obj_add_val(doc, obj, "transaction_valid_start", valid_start);
  ok = ok && yyjson_mut_obj_add_sint(doc, account, "shard_num", hiero->accountID.shardNum);
  ok = ok && yyjson_mut_obj_add_sint(doc, account, "realm_num", hiero->accountID.realmNum);
  ok = ok && yyjson_mut_obj_add_sint(doc, account, "account_num", hiero->accountID.accountNum);
  ok = ok && yyjson_mut_obj_add_val(doc, obj, "account_id", account);
  return ok ? obj : NULL;
}

static yyjson_mut_val *grdm_json_ledger_anchor(
    grdm_json_writer *writer, const grdw_ledger_anchor *anchor
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  if (!obj) { return NULL; }

  bool ok = yyjson_mut_obj_add_str(doc, obj, "type", grdt_ledger_anchor_to_string(anchor->type));

  switch (anchor->type) {
  case GRDT_LEDGER_ANCHOR_UNSPECIFIED:
    break;
  case GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID: {
    yyjson_mut_val *hiero = hiero_transaction_id(writer, &anchor->hiero_transaction_id);
    ok = ok && hiero && yyjson_mut_obj_add_val(doc, obj, "hiero_transaction_id", hiero);
    break;
  }
  default:
    ok = ok && yyjson_mut_obj_add_uint(doc, obj, "id", anchor->id);
    break;
  }
  return ok ? obj : NULL;
}

static bool grdm_json_add_account_balances(
    grdm_json_writer *writer,
    yyjson_mut_val *obj,
    const char *key,
    const grdw_account_balance *balances,
    size_t count
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *array = yyjson_mut_arr(doc);
  if (!array) { return false; }

  for (size_t i = 0; i < count; ++i) {
    const grdw_account_balance *balance = &balances[i];
    yyjson_mut_val *entry = yyjson_mut_obj(doc);
    yyjson_mut_val *pubkey = grdm_json_hex(writer, balance->pubkey, SIGN_PUBLIC_KEY_SIZE);
    yyjson_mut_val *amount = grdm_json_unit(writer, balance->balance);
    yyjson_mut_val *uuid = grdm_json_uuid(writer, balance->community_uuid);
    if (!entry || !pubkey || !amount || !uuid) { return false; }

    bool ok = yyjson_mut_obj_add_val(doc, entry, "pubkey", pubkey);
    ok = ok && yyjson_mut_obj_add_val(doc, entry, "balance", amount);
    ok = ok && yyjson_mut_obj_add_val(doc, entry, "community_uuid", uuid);
    ok = ok && yyjson_mut_arr_add_val(array, entry);
    if (!ok) { return false; }
  }
  return yyjson_mut_obj_add_val(doc, obj, key, array);
}

static bool grdm_json_add_encrypted_memos(
    grdm_json_writer *writer,
    yyjson_mut_val *obj,
    const char *key,
    const grdw_encrypted_memo *memos,
    size_t count
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *array = yyjson_mut_arr(doc);
  if (!array) { return false; }

  for (size_t i = 0; i < count; ++i) {
    const grdw_encrypted_memo *memo = &memos[i];
    yyjson_mut_val *entry = yyjson_mut_obj(doc);
    if (!entry) { return false; }

    bool ok = yyjson_mut_obj_add_str(doc, entry, "type", grdt_memo_key_to_string(memo->type));
    // an empty memo is a memo still: the type is what it carries, and the payload stays out
    // rather than appearing as an empty string that a reader would decode into nothing
    if (memo->memo.size) {
      yyjson_mut_val *payload = grdm_json_hex(writer, memo->memo.data, memo->memo.size);
      ok = ok && payload && yyjson_mut_obj_add_val(doc, entry, "memo", payload);
    }
    ok = ok && yyjson_mut_arr_add_val(array, entry);
    if (!ok) { return false; }
  }
  return yyjson_mut_obj_add_val(doc, obj, key, array);
}

static bool grdm_json_add_signature_pairs(
    grdm_json_writer *writer,
    yyjson_mut_val *obj,
    const char *key,
    const grdw_signature_pair *pairs,
    size_t count
) {
  yyjson_mut_doc *doc = writer->doc;
  yyjson_mut_val *array = yyjson_mut_arr(doc);
  if (!array) { return false; }

  for (size_t i = 0; i < count; ++i) {
    const grdw_signature_pair *pair = &pairs[i];
    yyjson_mut_val *entry = yyjson_mut_obj(doc);
    yyjson_mut_val *pubkey = grdm_json_hex(writer, pair->public_key, SIGN_PUBLIC_KEY_SIZE);
    yyjson_mut_val *signature = grdm_json_hex(writer, pair->signature, SIGN_SIGNATURE_SIZE);
    if (!entry || !pubkey || !signature) { return false; }

    bool ok = yyjson_mut_obj_add_val(doc, entry, "public_key", pubkey);
    ok = ok && yyjson_mut_obj_add_val(doc, entry, "signature", signature);
    ok = ok && yyjson_mut_arr_add_val(array, entry);
    if (!ok) { return false; }
  }
  return yyjson_mut_obj_add_val(doc, obj, key, array);
}

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_WRITER_C
