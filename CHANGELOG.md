# Changelog

Every release of gradido-blockchain-core, newest first. A date is the day the version was set in
`build.zig.zon`, which is not always the day a tag followed.

The version lives in `build.zig.zon`; `Doxyfile` carries it a second time for the generated
documentation. Until 1.0 the minor number moves for API changes, the patch number for fixes -- a
minor release may remove or reshape what is already there, and such a change is named here rather
than left to be discovered.

This file starts at 0.16.0. The version had stood at 0.15.2 since the zig build script was added
and did not move through the rewrite that followed, so there is no earlier boundary to write
entries against; the git history is the record for anything before this.

## 0.20.0 -- 2026-08-30

`src/mapping/runtime_from_json.c` is written again from the ground up, against the JSON reader
arnm 0.7.5 replaced its old one with. **This release needs arnm 0.7.5** and does not build against
0.7.4 or anything before it: the reader lost every call this mapping used to reach for.

**`grdm_complete_transaction_from_json()` lost its `flags` parameter**, which is what moves the
minor number. arnm 0.7.5 compiles yyjson with the non-standard extensions taken out rather than
defaulted off, so there is no switch left to pass: comments, trailing commas, `Infinity`, `NaN` and
a byte order mark are refused whatever a caller would have asked for, and strict RFC 8259 is the
only reading there is. Callers drop the last argument; nothing else about the call changed.

**The document format did not change.** A document written by 0.19.0 reads here byte for byte the
way it did there, and this release writes the same one.

### Changed

- **An optional member of the wrong type is read as absent.** arnm 0.7.5 hands a value out only as
  a handle and has no call that says what one is, so the literal `null` and a number in place of an
  array are the same answer from the read that wanted an array -- `ARNM_ERROR_INVALID_ENUM_TYPE`,
  with nothing to tell the two apart. For the three arrays and the two pairing members that answer
  is now taken as "nothing here", which is the reading `null` has always had here and is the price
  of keeping it. `"account_balances": 7` is therefore passed over where 0.19.0 refused it.
  `Refuses_ArrayThatIsNoArray` becomes `Reads_AnOptionalMemberOfTheWrongTypeAsAbsent` and says so.
  - Only optional members are read that way. Everything a transaction type owns is still required
    and still refused where it is missing or of the wrong type.
- **The root walk converts where it can, instead of collecting handles and reading afterwards.** A
  table converts a member where it stands and cannot convert one later, so `tx_nr`, the community
  uuid and the running hash go straight into the transaction as the walk meets them, and only the
  objects and arrays are filed as handles for the walk of their own that follows.
- **The three context scalars are asked for in a walk of their own.** `target_date`,
  `timeout_duration` and `previous_tx` share a union and only `transaction_type` says which of them
  a document owns, so they are left out of the root table and the one that matters is read by a
  one-field walk of the root once the type is known. That is one extra pass over the root's member
  chain for the four transaction types that carry such a scalar, and it stops at the key it wants.
  The same shape reads the pairing community uuid, and only where the root walk found it -- a local
  transaction never carries one and never pays for it.
- **An array's elements are taken all at once, into a buffer of handles.** `arnm_json_read_array()`
  replaced the iterator, and it fills a buffer or refuses -- it does not say how long an array was
  that did not fit. Each of the three arrays is read into sixteen handles on the stack, and the one
  that outgrows them is read again into a buffer sized by the document's node count, which no array
  of it can pass. A transaction of a real ledger does not reach the second read; the benchmark's
  200-balance transaction does, and the buffer comes back to the caller's allocator from its tail.
- **base64 is decoded over the document's own characters.** The reader has no field type that
  decodes base64 any more, so the memo payloads and `body_bytes` are borrowed as strings and spent
  by `arnm_binary_from_base64_insitu()`, then copied into the transaction's arena. Nothing is
  written into the arena that a refusal would have to take back, and a string that decodes to no
  bytes leaves an empty block rather than asking the arena for nothing.
- The arnm dependency moves from 0.7.3 to 0.7.5, in both places that name it: the
  `FetchContent_Declare(arnm ...)` in `CMakeLists.txt` and the `.arnm` entry of `build.zig.zon`.

## 0.19.0 -- 2026-08-26

The JSON write of a transaction needs a little over half the arena it needed yesterday, and the
document it produces is smaller. Every binary field goes in through one of the three calls arnm
0.7.3 added for it, and the writer is told how big the document will be before it starts.

**The document format changed**, which is what moves the minor number: `body_bytes` and the
encrypted memos are base64 now where they were hex. Everything else in the document is byte for
byte what it was.

**A document written by 0.18.0 will not always be refused by this release.** Every hex digit is
also a base64 character, so a hex payload whose length happens to be a multiple of four decodes
without complaint -- to different bytes. `body_bytes` of an even number of bytes is exactly that
case, which is most of them. Nothing in the document tells the two apart and this release does
not add anything that would; a document stored by 0.18.0 has to be read by 0.18.0.

**This release needs arnm 0.7.3.** It is the first thing here that does not build against
0.7.2 -- the three calls and the hint are all new, so arnm carries them as a patch, but they
have to be there.

### Changed

- **`body_bytes` and the encrypted memos are base64**, through
  `arnm_json_writer_add_base64()`; public keys, hashes and signatures stay hex, through
  `arnm_json_writer_add_hex()`. Which alphabet a field takes is a question about its reader:
  hex for the values a person compares against another tool's output, base64 for the payloads
  nobody reads by eye and whose length is what matters. Four characters per three bytes instead
  of two per one takes a third off the two longest fields in the document.
  - Both come from arnm, so the mapping still holds in a build without libsodium -- which is
    what kept it on hex until now. `bench_base64` measures arnm's pair against libsodium's:
    same text out of both, arnm's around eight times faster, because libsodium's runs in
    constant time and arnm's reads a table. Neither of these two payloads needs that property.
  - **`Base64Test` in `test_converter` crosses arnm's pair against libsodium's**, which is what
    makes "this is base64" more than arnm agreeing with itself: every length from 1 to 200
    encoded by both and compared, then each one's output read back by the other, plus the six
    vectors RFC 4648 prints pinned against both. Deliberately mutating a single character of
    arnm's alphabet fails two of those tests and a single entry of its decode table fails a
    third, so they are known to be load bearing rather than merely green.
    - Where the two differ is pinned as well: libsodium takes an `ignore` set and skips what it
      names, arnm takes none and refuses anything outside the alphabet. Both are defensible and
      only one of them is what this project writes.
  - `grdm_complete_transaction_from_json()` follows, and so do both of its sizing passes.
    `base64_binary_size()` is the one place that answers how long a decoded payload is, because
    a sizing that came out under what the read then writes is an arena the read runs past.
- **The local `add_hex_fixed()` and `add_hex_block()` are gone**, and with them the buffers they
  formatted in. What that is worth, measured over
  48762 real transactions of the Gradido Akademie ledger, on the one whose document is the
  largest at 3382 bytes:
  - The arena a write needs at its peak falls from **24208 to 10448 bytes**, and what one
    still holds after the call from 11888 to 6944. Of the 13760 that went, base64 carries
    1232, the pool hint 1968, and the two hex calls the rest.
  - **The document itself is 17 percent shorter**: the largest of the ledger goes from 3382 to
    2792 bytes, which is `body_bytes` and one memo losing a third of their length each. Most of what went is the serializer's working buffer: it reserves
    `str_len * 6 + 16` before writing a string, against the possibility that every byte escapes
    to `\uXXXX`, and hex escapes to nothing. `add_hex()` puts the text in already quoted, as a
    raw value, which is reserved for at `str_len + 2`. On that document the old buffer grew to
    12320 bytes for a text of 3382.
  - Of what an arena still holds after the call, 1792 are
    the scratch that `add_hex_block()` used to hex into: it was allocated, copied out of, and
    handed back -- and an arena never took it, because the document's own copy of the same
    characters was allocated on top of it. There is no scratch and no copy now; the digits are
    formatted straight into the document.
  - The peak scales with the longest field rather than with the document, so the further a
    transaction is from average the more this is worth. A 2 KiB memo used to ask for about
    24 KiB of working buffer on its own.
- **Community uuids are written with `arnm_json_writer_add_uuid()`,** and the local
  `add_uuid()` goes with the hex helpers. That was the last stack buffer in the file and the
  last copy out of one; `arnm/converter.h` is no longer included here, nothing in the mapping
  reaching for it any more.
  - It does not move the peak on this ledger, and is not expected to: a uuid renders as 36
    characters and `body_bytes` as 982, so it is the hex field that decides how far the
    serializer's buffer grows. What it removes is the pessimistic reservation of `36 * 6 + 16`
    per uuid, which is the deciding one on a document with no long hex field in it -- a
    register-address or a community-root transaction with no memo.

- **The writer is told how big the document will be before it starts.** A walk over the
  transaction counts the values it will hold and the bytes its copied strings will take, and
  hands both to `arnm_json_writer_init()` as an `arnm_json_writer_hint`. yyjson's two pools then
  open once at that size instead of doubling their way there and keeping every chunk they
  outgrew -- which on the largest transaction of this ledger was 1968 bytes of the 6712 the
  document occupied.
  - The walk is exact: it was checked against the real node count of all 48762 documents of the
    Gradido Akademie ledger, with no disagreement, and predicted the 1968 bytes to the byte.
  - It does not have to stay exact, and that is deliberate. A field added to the writer and
    forgotten in the walk costs one extra chunk -- the growth that would have happened anyway --
    so this is not a second place where a field can be got wrong, only one where it can be got
    cheap.

- The four branch writers and `add_arrays()` return nothing now. Each of them only ever passed
  on what the hex helpers answered, and those answers are the writer's own to keep since it is
  the writer that formats; `grdm_json_from_complete_transaction()` reads them off it at
  `arnm_json_writer_write()`, which it already did for every field added without a test.
  `add_transaction_detail()` still answers, for the transaction type it has no layout for.
- The arnm dependency moves from 0.7.2 to 0.7.3, in both places that name it: the
  `FetchContent_Declare(arnm ...)` in `CMakeLists.txt` and the `.arnm` entry of
  `build.zig.zon`. Each pins the release archive by URL and by hash, so a moved tag or a
  regenerated archive is caught rather than taken.

### Notes

- No signature, result code or field changed, and neither did a single byte of any document
  this writes -- `grdm_complete_transaction_from_json()` reads what it read before. A caller
  updates nothing. What a caller may want to revisit is the size of the arena it hands in.
- `ARNM_ERROR_DESTINATION_BUFFER_TO_SMALL` is no longer among the answers: it came from
  `add_hex_fixed()` checking a field against its stack buffer, and there is no stack buffer
  left to outgrow. It was never named in the public header.

## 0.18.0 -- 2026-08-25

`grdr_complete_transaction` gains a second pair of banks. It could already be built from the
wire and it can now be written as JSON and read back from it, through arnm's `json_writer.h`
and `json_reader.h` -- the same yyjson that has been compiled into this library since 0.17.0
without anything reaching for it.

Nothing that was there moved. Two headers, two translation units, one test binary and one
benchmark are added; no existing signature, result code or field changed. This is a minor
rather than a patch because the public surface grew, not because anything in it shifted.

### Added

- **`mapping/json_from_runtime.h` -- `grdm_json_from_complete_transaction()`.** Writes a whole
  runtime transaction as one JSON object into a block the caller owns. Takes an allocator and
  the `ARNM_JSON_WRITE_*` flags, so the same call serves a minified payload and the pretty form
  a person reads.
- **`mapping/runtime_from_json.h` -- `grdm_complete_transaction_from_json()`.** The way back,
  and an exact inverse: every field the writer sets down is read here, including the ones the
  transaction type in hand does not use, so a round trip is a copy and not a reconstruction.
  The transaction's arena is sized in one pass over the parsed document before a byte of it is
  copied -- the same shape `grdm_complete_transaction_from_wire()` has always had.
- **`test_json`**, a googletest binary that builds its fixtures rather than decoding them and
  therefore needs no libsodium: every branch of both unions written, read and compared, plus
  what a malformed document is refused for; one case whose arrays are wide enough -- 64
  balances, 8 memos of differing lengths, 32 signatures -- that a walk which stalls or slips by
  one cannot pass it; and one that shuffles `transaction_type` to the end of the document.
  `test_runtime` gains one case that puts a transaction which really came off the wire through
  the same passage.
- **`bench_json`**, which carries one transfer in all three shapes -- protobuf, minified JSON,
  pretty JSON -- and prints their sizes beside the cost of moving between them. It needs no
  libsodium either, so it builds wherever the library does. The protobuf row is a ruler rather
  than a competitor: it ends in the same `grdm_complete_transaction_from_wire()` and opens its
  runtime arena from the host exactly as the JSON reader does, so the two reads are directly
  comparable. Three fixtures: a small transfer, a large one whose payload is what it mostly
  costs, and a wide one -- 200 balances, 200 signatures, no memo -- that is no transaction
  anyone will see and exists only to isolate what a document's element count, rather than its
  payload, is worth.

### Notes

- **The document's shape.** Binary travels as lowercase hex -- `arnm_binary_to_hex()`, two
  characters a byte, no separators -- because base64 in this project needs libsodium and the
  mapping has to hold in a build without it. Community uuids take the canonical 8-4-4-4-12
  form. Enumerations are written as their enumerator's own spelling,
  `"GRDT_TRANSACTION_TRANSFER"` rather than `2`, so a value inserted into an enum cannot
  silently change what an old document means. The full shape is documented at
  `grdm_json_from_runtime`.
- **Reading an enumerator back walks `grdt_*_to_string()` in reverse** rather than carrying a
  second table. A name added to an enum is therefore readable the moment it is spelled there,
  and there is no second list to fall out of step with the first.
- **What a document may leave out** is what the transaction does not own: a transfer has no
  `target_date`, a local transaction has neither pairing member, and the three arrays may be
  absent as well as empty. Any of those may also be written as the literal `null`, which says
  the same as leaving it out. Everything a type does own is required, and a missing one is
  refused rather than defaulted -- a silent zero in a public key or an amount is the expensive
  kind of forgiveness.
- The 0.17.0 note that "nothing of this project includes a JSON header" no longer holds: the
  two headers above include `arnm/json_writer.h` and `arnm/json_reader.h`. The half of that
  note that still stands is the one that mattered -- no installed header names yyjson, and
  nothing of yyjson reaches a consumer.
- **The reader walks every object once instead of asking it for members by name.** A JSON
  object keeps its members in a chain, so `arnm_json_reader_get_*(reader, key)` walks that
  chain until the key turns up, and asking one object for all of its keys walks it once per
  question. The root object carries twenty-odd members and this mapping wants nearly all of
  them, which is the worst shape that arithmetic has. Now each object is walked once, every key
  is handed to a recogniser that answers what it is with a `switch` on its length and at most
  one `memcmp`, and the value is filed in a slot indexed by field -- the shape
  `geo_address_search_c` uses on planet dumps. Reading afterwards is array indexing with no
  searching left in it.
  - Measured on this machine, zig `ReleaseFast`, the read of one transaction: **small transfer
    1.3 us → 923 ns (−29 %)**, **large transfer 3.7 → 3.2 us (−15 %)**, **wide transfer 38.4 →
    35.3 us (−8 %)**. The small one gains most because it is nearly all root lookups; the wide
    one least because its time is mostly the hex of 400 elements, which this does not touch.
    The writer has no such cost -- it appends and never looks anything up.
  - Falling out of it: **member order stopped mattering**. The walk collects first and decides
    afterwards, so a document that puts `transaction_type` after the detail member it governs
    reads like one that puts it first. A `null` written in place of an optional member counts
    as absent, the same reading arnm's own reader takes.
  - Every `case` label is `KEY_LEN(GRDM_JSON_KEY_X)`, worked out by the compiler from the key's
    own literal. Hand-counted lengths were tried first and four of them were wrong, silently --
    a mistyped length simply never matches, and the member then reads as absent. Derived from
    the literal it cannot drift, and two keys of equal length become a duplicate `case`, which
    is a compile error exactly where a second look at the first byte is needed.
- **Writing costs more than reading on a transaction that is nearly all payload**, and
  `bench_json` is where that is visible: a 7.3 KB document takes about 5 us to write and just
  under 4 us to read in a zig ReleaseFast build, against 1.1 us for the same transaction
  arriving as protobuf. The reason is the second copy in `add_hex_block()` -- memo and body
  bytes are hexed into scratch and then copied into the document. Borrowing instead would mean
  keeping every such block alive until the render and freeing it after, bookkeeping this
  mapping does not carry today; the trade is written down at the function rather than left to
  be rediscovered.
- Not verified: MSVC and Windows, neither of which can be built from this checkout. The zig
  build (`x86_64-linux-gnu`) and the CMake build were both run, the latter also with
  `-DENABLE_SANITIZERS=ON`; UBSan and ASan with leak detection are clean over the new tests.
  The benchmark figures above are one machine's, in a zig `ReleaseFast` build; the CMake build
  puts the same rows further apart, not closer.

## 0.17.1 -- 2026-08-24

`-Wall -Wextra -Wconversion` on the C this project owns, in both builds, and the tree is clean
under them. Most of what they found was a narrowing that needed to say so in a cast. A handful
were not, and those are below.

Nothing here removes or reshapes what was there: every signature, every existing result code and
every value a call already answered with is unchanged, so code built against 0.17.0 keeps
building and keeps behaving. Three calls gained a refusal they did not have, which is the one
thing in this release a caller can notice -- named under **Changed** rather than left to be
discovered, since the rule at the top of this file would otherwise have put it in a minor.

### Added

- **Both builds compile the first-party C with `-Wall -Wextra -Wconversion`** (`/W4` plus
  `/w14242 /w14244 /w14267` on MSVC, which has no `-Wconversion`). The same flags arnm holds
  itself to, so a build no longer accepts on one side what it rejects on the other. Nothing is
  `-Werror`: zig hides C warnings on a successful build and turns them into errors on a failing
  one, so the CMake build is where a narrowing gets read without the tree being broken already.
  - Two source sets stay exempt, the same two `lint.sh` skips: `third_party/` is vendored, and
    `src/data/proto/` is regenerated by `update_proto.sh`, which would discard a fix there on
    its next run. `third_party/` is reached through `include_directories(SYSTEM ...)` rather
    than only exempted per file -- `r128.h` does its arithmetic inline, so its warnings arrive
    through our own translation units and a per-file exemption never sees them.
  - The googletest translation units stay exempt as well.

### Changed

- **Three narrowings that carry a figure off the wire are now refused rather than wrapped**,
  each answering `ARNM_ERROR_RESOURCE_SIZE_EXCEED`. The only change a caller can see: input that
  used to be accepted and silently truncated is now rejected, so a consumer switching on the
  result of these calls has one more value to handle. Nothing that succeeded before fails now.
  - `memory_block_from_pbtools()`: pbtools counts a byte string in a `size_t`, arnm allocates in
    a `uint32_t`. On a 64 bit host a length past `ARNM_MAX_ALLOC_SIZE` wrapped into a smaller
    request, and the `memcpy` that followed wrote the full length into it.
  - `gradido_deferred_transfer_from_pbtools()`: the wire carries a timeout in a `uint64_t` and
    `grdw_gradido_deferred_transfer::timeout_duration` is a `uint32_t` -- about 136 years. A
    value beyond that became a short timeout instead of an error.
  - `grdm_complete_transaction_from_wire()`: `calculate_memory_size()` adds up wire-supplied
    counts and sizes in a `size_t` and the arena is opened with a `uint32_t`, so a sum past
    4 GiB opened a small arena that every later allocation then ran past.
- `grd_pb_result` values returned as `arnm_result` carry an explicit cast. The two are separate
  enum types by construction -- `result.h` counts the project's codes on top of arnm's reserved
  range -- and the cast now says that the crossing is the design.

### Fixed

- **Three undefined negations of `INT64_MIN`** in `src/data/unit.c`. Each wrote `-value` on a
  signed value that had just been tested for being negative, which is undefined for the one
  value whose magnitude the type cannot hold. All three now negate the unsigned copy, which is
  defined and gives the magnitude the code was after.
  - `grdd_unit_round_to_precision()`, where the `rounded > INT64_MAX` check below it could not
    do its job until the value reaching it was right.
  - `grdd_unit_calculate_decay()`, on the duration it raises the decay factor by.
  - `grdd_unit_to_string()`, which is the reachable one: a precision of 4 rounds nothing, so
    `INT64_MIN` arrives at the formatting untouched. The sign is written once now and the digits
    are carried in a `uint64_t` from there on. On gcc and clang the wrapped value happened to
    read back as the right magnitude, so the printed digits were already correct -- what this
    ends is the UndefinedBehaviorSanitizer report, and the day a compiler stops being kind.
    `toString_Int64Min` pins the output at every precision, including the three that
    `grdd_unit_round_to_precision()` refuses because rounding lands past `INT64_MAX`.
- **`bench_numberToString` measured the decay of a different number than it drew.** It called
  `abs()` -- an `int` function -- on a 64 bit value, so every draw above `INT_MAX` was truncated
  before it was ever made positive. It is `llabs()` now.
- **`bench_numberToString` printed `int64_t` through `%lld`.** Right on Windows, wrong on the
  LP64 platforms where `int64_t` is `long`; it is `PRId64` now.
- `grdd_timestamp_gt()` and `grdd_timestamp_lt()` spell out the `&&` inside the `||`. C already
  grouped it that way, so no comparison changes -- the parentheses are what a reader no longer
  has to work out.
- The `#pragma warning(push/disable/pop)` pairs in the five `*_pb_compat.h` headers are behind
  `#ifdef _MSC_VER`. They are MSVC's, and being in public headers they were handing every gcc
  and clang consumer three `-Wunknown-pragmas` per include.
- Two `arnm_result result = ARNM_ERROR_NOT_INITIALIZED;` declarations that nothing read, in
  `wire_from_pbtools.c` and `pbtools_from_wire.c`. Nothing was swallowing a result; both were
  left over from a body that has since become a single `return`.
- `grdr_complete_transaction_get_account_balance_for_public_key()` and the signature pair loop
  in `grdi_validate_context()` walk their `size_t` counts with a `size_t`.

## 0.17.0 -- 2026-08-24

hostmem is now [arnm](https://github.com/gradido/arnm), and the dependency moves from hostmem
0.4.0 to arnm 0.7.2. The library is the same one under a new name -- arnm 0.5.0 renamed every
symbol it has -- so most of this release is a prefix substituted in 50 files. What is not
mechanical is named below.

### Changed

- **Every `hostmem_` is an `arnm_`, every `HOSTMEM_` an `ARNM_`, and the headers moved from
  `hostmem/` to `arnm/`.** This reaches the public headers of this project: `arnm_result` is
  what `grd*` functions return, `arnm_memory_block` is what they take, and `arnm *` is the
  allocator. A consumer substitutes the prefix in both cases and the include path once;
  no signature moved and no result code changed its value.
- **`arnm_init_arena()` and `arnm_init_arena_borrow()` live in `arnm/arena.h`,** which arnm
  0.6.0 split out of `arnm/memory.h`. The five translation units that set an arena up include
  that header instead; it includes `arnm/memory.h`, so nothing else had to be added.
- **The arena is asked how much it has left instead of being measured.** arnm 0.6.0 made the
  allocator an opaque 32 byte struct, so `allocator->capacity - allocator->last_index` -- the
  expression that handed pbtools the rest of the arena as workspace -- is no longer readable.
  arnm 0.7.1 added `arnm_arena_remaining()`, which answers that same subtraction, and the four
  sites in `src/data/wire` use it.
  - `grdw_pb_workspace_take()` also tells host mode from a full arena through
    `arnm_is_arena()` rather than through a capacity of 0. The two result codes it answers with
    are unchanged: `ARNM_ERROR_INVALID_PARAM` for an allocator with no arena to lend,
    `ARNM_ERROR_OUT_OF_MEMORY` for one with nothing left.
  - `test_pbtools` read `last_index` and `capacity` to assert that an arena was full, empty, or
    holding a kept workspace. All three now read `arnm_arena_remaining()`, the "empty" figure
    taken from the arena itself right after `arnm_init_arena()` rather than written out as a
    constant.

### Notes

- arnm carries a copy of yyjson in its tree since arnm 0.7.2, for `arnm/json_reader.h` and
  `arnm/json_writer.h`. Both builds of this project compile arnm's sources into their own
  target rather than linking a second archive, so both compile that one source too and add its
  include path privately. Nothing of this project includes a JSON header, and no installed
  header names yyjson.
  - It had to be that release: yyjson was a git submodule up to arnm 0.7.1, and neither a
    GitHub tarball nor `zig fetch` carries one -- both deliver `third_party/yyjson` as an empty
    directory, and arnm's own `build.zig` stops the build when it finds it that way.

## 0.16.0 -- 2026-08-19

The release that finishes moving everything general out of this project. What is left is the
Gradido domain -- transactions, crypto, the wire and runtime types -- plus the handful of
conversions that need libsodium. Containers, timing, allocation and the plain conversions now
come from [hostmem](https://github.com/einhornimmond/hostmem), which this project depends on
rather than carries.

### Removed

These moved to hostmem and are gone from this library's headers. A consumer swaps the include and
the prefix; the signatures and result codes did not change.

- `utils/bucket_vector.h`, `utils/duration.h`, `utils/memory_block.h`, `utils/mono_timer.h` --
  entire headers, now `hostmem/bucket_vector.h` and friends.
- From `utils/converter.h`: the number conversions, and with this release the hex and uuid pairs
  as well.
  - `grdu_binary_to_hex()` -> `hostmem_binary_to_hex()`
  - `grdu_binary_from_hex()` -> `hostmem_binary_from_hex()`
  - `grdu_uuid_to_string()` -> `hostmem_uuid_to_string()`
  - `grdu_uuid_from_string()` -> `hostmem_uuid_from_string()`
- `UUID_BINARY_SIZE` from `const.h`, which duplicated hostmem's `HOSTMEM_UUID_BINARY_SIZE`. Two
  definitions of the same 16 can drift; one cannot. The ffi accessor `grdc_uuid_binary_size()`
  stays, for callers that reach this library without headers to read it from.

### Added

- `grdu_secret_to_hex()` and `grdu_secret_from_hex()` in `utils/converter.h` -- the hex pair for
  material that has to keep quiet about itself, built on libsodium, which hostmem cannot offer
  because it links no crypto library. Same arguments and same result codes as hostmem's fast
  pair, so a caller swaps one for the other and changes only the timing. The decoding half wipes
  its output with `sodium_memzero()` rather than `memset()` when a string does not decode.
  - The cost, over 32 bytes in a ReleaseFast build: about 12 ns against 6 for encoding, about
    94 ns against 9 for decoding. Rounded because the figures move a little between runs; the
    ratio is what holds. The decoding gap is the wider one because `sodium_hex2bin()` carries a
    state machine that cannot be vectorised at all.
  - Reach for these when the bytes are a key, a seed or a passphrase. Hashes, transaction ids and
    public keys are what hostmem's pair is for.
- Both halves of that comparison run in `bench_numberToString`, under `uuid from string`,
  `uuid to string`, `32 bytes to hex` and `32 bytes from hex`. The benchmark stayed in this
  project on purpose: hostmem links no crypto library and so has nothing to measure itself
  against.

### Changed

- **`buffer_size` now counts the terminator**, the way `snprintf` counts it, in
  `grdd_timestamp_to_string()`, `grdw_hiero_account_id_to_string()` and
  `grdw_hiero_transaction_id_to_string()`. A buffer of exactly the character count used to be
  accepted and is now refused. The return value is unchanged: the character count without the
  terminator, which is what the matching `_calculate_string_size()` gives and what a caller adds
  one to. The contract was undocumented before and is now written at each declaration.
  - A caller that sized a buffer from `_calculate_string_size()` and passed that figure straight
    through was overflowing it by one byte and now gets a refusal instead. Add one.
- The hostmem dependency moved from 0.2.0 to 0.4.0. Along the way `hostmem_init_arena_static()`
  was renamed to `hostmem_init_arena_borrow()`, which this project's one call site follows.

### Fixed

- **A one byte stack overflow** in `grdd_timestamp_to_string()` and
  `grdw_hiero_account_id_to_string()`: both compared the buffer against the character count and
  then wrote a terminator past it. AddressSanitizer reports it; the test suite was asserting the
  broken contract rather than catching it. See *Changed* above for what callers have to do.
- `grdw_hiero_transaction_id_to_string()` measured its parts one at a time, so the `@` between
  them landed in the byte the account id had used for its terminator. It now measures the whole
  line up front. It also used to leave the buffer without a terminator when the timestamp could
  not be printed, and returns 0 for that case instead.
- `grdw_hiero_transaction_id_calculate_string_size()` disagreed with the writer about a valid
  start whose nanos fall outside 0..999999999 -- something a wire type may carry and a timestamp
  may not. It added the account id's length to the timestamp's 0 and returned a figure that
  measured nothing, while `grdw_hiero_transaction_id_to_string()` refused the same value. Both
  answer 0 now, so sizing a buffer from the one and writing with the other cannot disagree.
- **A heap buffer overflow in the uuid parser.** The version in 0.15.2 counted separators instead
  of checking their positions, and every missing one turned two more characters into an output
  byte: a 36 character string of pure hex wrote 18 bytes into the 16 the caller owns. A string of
  36 separators returned success without writing anything at all. The fixed parser shipped in
  hostmem 0.4.0 as `hostmem_uuid_from_string()`; anyone still calling `grdu_uuid_from_string()`
  from an older release of this library should move.
- The CMake build with `-DENABLE_TESTS=ON -DUSE_SODIUM=OFF` did not link. `tests/unit` carried a
  duplicate of the top level's libsodium block with the condition inverted, so a build without
  sodium fetched it anyway and defined `USE_SODIUM` for the test sources while the library left
  its crypto files out. The duplicate is gone, and the four test binaries that need sodium are
  declared only when it is there -- the same split `build.zig` already made. `bench_numberToString`
  had the same unconditional link and follows now.
- `ctest --test-dir <build>` found no tests: `enable_testing()` was called only in `tests/unit`,
  so the top level wrote a `CTestTestfile.cmake` that did not descend into it.
- Since hostmem 0.3.0 ships a `CMakeLists.txt` of its own, `FetchContent_MakeAvailable()` would
  have called `add_subdirectory()` on it -- and because this project's `ENABLE_TESTS` and
  `ENABLE_BENCHMARKS` carry the same names as hostmem's, `-DENABLE_TESTS=ON` would have built
  hostmem's googletest suite too. `SOURCE_SUBDIR` now says populate only.
- Memory leaks in the tests: five in `test_pbtools` and one in `test_runtime` left the heap arena
  from `hostmem_init_arena()` standing, and three `fromBase64()` results were never freed.
  `test_runtime` also never released the arena that `grdr_complete_transaction` carries of its
  own. The full suite now passes under AddressSanitizer, UndefinedBehaviorSanitizer and
  LeakSanitizer together, with no suppressions.

### Notes

- Of the two hex pairs only `grdu_secret_*` runs in constant time; hostmem's does not, and the
  group warning in `utils/converter.h` says so with the reason. hostmem's encoder computes its
  digits rather than looking them up, and its vectorised body really is branchless -- but the
  scalar path beside it, which takes the remainder and takes short inputs whole, compiles to a
  compare and a jump on the nibble. Writing that conditional as an arithmetic mask does not move
  it; the compiler turns it back into a branch, and an unoptimised build has no vector path at
  all. Verified in the disassembly, not assumed.
- `const.h` now includes `hostmem/converter.h`, which is where `HOSTMEM_UUID_BINARY_SIZE` comes
  from. A consumer that included only `const.h` gets that header along with it.
