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
