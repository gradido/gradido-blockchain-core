# AGENTS.md – Gradido Blockchain Core

A **C11** static library: transaction data structures, protobuf mapping, validation, crypto and
a small set of containers. C++ appears only in the tests (googletest). Everything under
`src/` and `include/` must compile as C, and as C++ when a consumer includes it.

Two things are non-negotiable and have their own chapters below: the **portability contract**
and the **commenting standard**. The rest is what saves you a wasted afternoon.

----------

## Build and test

`zig build` is the primary build. **`-Dtarget` is mandatory** — a native build without it fails
in this checkout.

```bash
zig build -Dtarget=x86_64-linux-gnu -Dtests=true -Dbenchmarks=true -Dsodium=true
```

| Option | Meaning |
|---|---|
| `-Dtarget=` | required, e.g. `x86_64-linux-gnu`, `x86_64-linux-musl`, `x86_64-windows-gnu`, `aarch64-macos` |
| `-Dtests=true` | build the googletest binaries |
| `-Dbenchmarks=true` | build the `bench_*` binaries |
| `-Dsodium=true` | build the crypto module; `test_crypto`, `test_pbtools`, `test_runtime` and `bench_crypto` need it |
| `-Dsanitize=undefined_behavior` | UBSan; `thread` for TSan. AddressSanitizer only via CMake |

Binaries land in `zig-out/bin`. Run them all at once:

```bash
./run_all.sh              # everything, one line per binary
./run_all.sh --tests      # skip the benchmarks
./run_all.sh -o memory    # only binaries whose name contains "memory"
```

`run_all.bat` is the Windows counterpart. Test binaries print only their verdict unless they
fail; benchmarks always show their output, because their output *is* the result.

The CMake build exists for MSVC and for AddressSanitizer
(`-DENABLE_TESTS=ON -DENABLE_SANITIZERS=ON`). Keep both build files in sync when you add a
source file — CMake globs, `build.zig` lists test binaries by name.

**Tests cap their own memory.** `tests/unit/src/memory_limit.h` sets `RLIMIT_AS` to 2048 MB on
Linux, skipped under sanitizers. Raise it with `GRD_TEST_MEMORY_LIMIT_MB=8192`, disable with
`0`. Include it in every new test binary. This exists because a boundary test once allocated
64 GB before anyone could hit Ctrl-C.

----------

## Portability contract

Targets: Linux (glibc and musl), Windows (MSVC and MinGW), macOS. MSVC cannot be tested from a
Linux checkout — say so instead of implying you did.

- **No C++ headers in C code.** `<cstdint>` in a `.c` file breaks every C compiler. Use
  `<stdint.h>`.
- **Every public header compiles on its own**, in C and in C++. Verify it:
  ```bash
  gcc -std=c11   -Iinclude -Ithird_party -Ithird_party/pbtools -fsyntax-only -x c   header.h
  g++ -std=c++17 -Iinclude -Ithird_party -Ithird_party/pbtools -fsyntax-only -x c++ header.h
  ```
  Include what you use — a type borrowed from another header's includes breaks the day that
  header is tidied.
- **The `static_assert` fallback must exclude C++**, where it is a keyword and not a macro:
  ```c
  #if !defined(__cplusplus) && !defined(static_assert)
  #define static_assert _Static_assert
  #endif
  ```
- **No legacy or POSIX-only headers** without a guard. `<memory.h>` is a removed SVID relic —
  `<string.h>` does the same. `<unistd.h>`, `<sys/*.h>` need an `#ifdef`.
- **Platform branches carry their own includes.** A `#ifdef _WIN32` block that calls `exit()`
  needs `<stdlib.h>`; that `windows.h` happens to drag it in is luck, not a contract.
- **Every `.c` includes its own header first**, so the compiler checks declaration against
  definition.
- **`src/data/proto/gradido/` and its headers are generated** by `update_proto.sh` (pbtools),
  which deletes the folders before regenerating. Never edit them; fix the generator or the
  `.proto` instead. `third_party/` is vendored — same rule.

----------

## Memory and result contract

`grd_memory` is either plain malloc/free or a bump arena, chosen by what the caller passes —
`NULL` means malloc/free. Read `include/gradido_blockchain_core/memory.h` before touching an
allocation path. The invariants:

- **Everything counts in `uint32_t`**: sizes, counts and indices. A size that would not fit
  yields `GRD_ERROR_ARITHMETIC_OVERFLOW`. Where a bound is known at compile time, use
  `static_assert` instead of a runtime check.
- **Sizes are passed in, never stored.** Freeing and resizing need the size the caller
  allocated with. A wrong size moves the arena index by the wrong amount and hands the same
  bytes out twice. `grdu_memory_block` keeps pointer and size together when that bookkeeping
  should not be the caller's job.
- **Every size rounds up to a multiple of 8**, which keeps returned pointers 8-byte aligned.
- **`GRD_WARNING_ARENA_MEMORY_NOT_RECLAIMED` is neither success nor failure**: the operation
  happened, the memory did not come back. Handle it explicitly at each call site, and compare
  against the exact value. No `if (ok(result))` helper — a reader must see that this warning
  can arrive here and be able to decide anew what it should mean.
- **Failures leave every output untouched.** Initialisation functions write all fields and
  read none, so uninitialised storage is a valid input.

Prefixes: `grd_` core · `grdu_` utils · `grdd_` data · `grdt_` types · `grdw_` wire ·
`grdr_` runtime · `grdm_` mapping · `grdi_` interactions · `grdc_` crypto.

----------

## Formatting

`clang-format` with the repo's `.clang-format` (LLVM base, 100 columns, 2 spaces). `./lint.sh`
formats `src/`, `include/`, `tests/unit/src/` and `benchmarks/src/` — `.c`, `.h` and `.cpp`,
**at any depth**, because it walks the trees with `find`. A ladder of `src/**/**/*.c` patterns
does not do that: bash expands `**` like a single `*` unless `globstar` is set, so such a
ladder stops at a fixed depth and skips anything below it without a word.

Two trees stay out on purpose: `third_party/` is vendored, and `*/data/proto/` is generated by
`update_proto.sh`, which deletes and rewrites those folders — formatting them only produces a
diff the next generator run discards.

The tree is kept formatted, so a run is a no-op unless you left something behind — check a
single file with `clang-format --dry-run -Werror <file>`.

Format the files you touched, and keep a reformatting pass in its own commit when a file has
drifted: mixing whitespace with a change buries the change.

----------

## How to work in this repository

- **Measure before you claim.** Object sizes, timings, "this is faster" — run it. A compiler
  often optimises away exactly the thing you were about to take credit for.
- **Prove the test bites.** After fixing something, revert the fix, watch the new test fail,
  then put it back. A test that never failed proves nothing.
- **Cap the memory before probing a boundary.** `ulimit -v` for a scratch program,
  `memory_limit.h` for a test binary. Prefer a small static arena over malloc when the point
  is that a request must be *refused*.
- **Say what you did not verify.** MSVC, Windows, a build you could not run — name it. Silence
  reads as confirmation.
- **Keep the diff about the change.** Unrelated reformatting, drive-by renames and speculative
  refactors make review expensive.

----------

## C Modules (Doxygen)

- Every public C header MUST define exactly one module using `@defgroup`.
- The module MUST wrap the API using `@{` … `@}`.

```c
/** @defgroup grdd_unit grdd_unit
  *  @ingroup data
  *  @brief Fixed-point GDD (scale 10^4)
  *  @{
  */

// API here

/** @} */
```

- Modules MUST belong to a parent via `@ingroup` with there folder name (`data`, `utils`, etc).
- If the parent does not exist, DEFINE it once:

```c
/** @defgroup data Primary Data Structures */
```

----------

### Rules

- One module per header
- All public API must be inside the module block
- Use flat, stable identifiers (`grdd_unit`)

----------

### Goal

Ensure all APIs appear in Doxygen “Modules” with a clear hierarchy.

## Commenting Guidelines for AI Agents, Poetic Precision – Dual-Layer Commenting Standard

## Core Model

All comments consist of two aligned layers:

### 1. Technical Layer (Ground Truth)

Hard, verifiable specification.

Must include:

- parameters, types, constraints
- scaling rules (e.g. fixed-point 10^4)
- edge cases
- return behavior
- overflow / limits
- deterministic rules

Rules:

- no ambiguity
- no metaphor instead of facts
- fully sufficient for implementation without poetic layer

----------

### 2. Semantic Layer (Poetic Precision)

Describes system behavior as **natural process perception**.

Allowed:

- flow, cycle, rhythm, transition
- dissolve, emerge, settle, converge
- stream, season, tide, growth, decay
- backward projection / forward preparation

Constraints:

- must not change technical meaning
- must not introduce moral framing
- must not replace constraints with imagery
- must stay fact-consistent

Purpose:

- reduce cognitive load
- improve conceptual continuity
- express system behavior as continuous process

----------

## Forbidden Transformations

Do NOT convert:

- constraints → metaphors only
- limits → value judgments
- edge cases → poetic ambiguity
- precision → narrative softness

----------

## Writing Principle

Each comment is:

> deterministic logic + natural process description

Never:

- poetry instead of specification
- specification without semantic flow

----------

## Internal Objective

Increase:

- readability of complex systems
- continuity of mental model
- semantic coherence across codebases

Without reducing:

- precision
- determinism
- auditability

## The `@whisper` Tag – Optional Poetic Signature

The `@whisper` is an optional, poetic one‑liner at the end of a Doxygen comment. It is **not required for every function**, but encouraged for functions that carry significant meaning – especially core economic functions (e.g., decay, growth, time‑based transformations).

### When to Use a `@whisper`

- **High‑impact functions** (e.g., `grdd_unit_calculate_decay`) should almost always have a `@whisper`. They are the heart of Gradido and deserve a quiet, memorable line.
- **Medium‑impact functions** (e.g., `grdd_unit_from_string`) may have a `@whisper` if a fitting image or quote comes naturally.
- **Low‑level helpers** (e.g., internal byte swappers) rarely need a `@whisper`. If in doubt, omit it.

### What a `@whisper` Must Do

- Briefly describe the **essence of the function** in poetic, calm language.
- OR quote a **famous person** (with attribution) that fits the function’s purpose. Keep quotes short and universally respectful.
- Be **subtle, never loud**. No exclamation marks, no moralizing.
- End without a period.

### What a `@whisper` Must NOT Do

- Replace or compensate for missing technical documentation.
- Preach (“you should”, “it is good to”).
- Drift into irony or sarcasm.

### Editing Existing `@whisper` Lines

- **Never delete** an existing `@whisper` unless it is completely unrelated to the current function’s behavior.
- **Updating** is allowed only when the function itself has changed so much that the old whisper no longer fits. In that case, rewrite it to match the new purpose while preserving the poetic tone.

### Respect Existing `@whisper` Lines

- **Never delete** an existing `@whisper` unless it has become completely unrelated to the function’s current behavior.
- **Updating** is allowed only when the function itself has changed so much that the old whisper no longer fits. In that case, rewrite it to match the new purpose while preserving the poetic tone.
- Do not change a `@whisper` just for stylistic preference. If it works, let it be.

### Standard Comment Structure (Flexible)

The structure is a suggestion, not a straitjacket. Adapt length and order as needed.

```c
/**
 * @brief One-line summary (poetic but clear).
 *
 * A few sentences explaining what the function does. Use calm, image‑rich
 * language. Mention technical details naturally within the flow.
 *
 * @param[in/out] name   Description.
 * @return               Exact return values (e.g., true/false, number of bytes).
 * @note (optional)      Important constraints.
 * @whisper (optional)   Short poetic line, no period.
 */
```

### Core Functions – Reference to Gradido Philosophy Allowed

The following functions implement Gradido’s foundational concepts. For these only, comments may explicitly mention the relevant natural laws, pillars, or the triple good.

#### Which Concepts Exist?

- **Three Natural Laws**: Symbiosis & Cooperation, Cycle of Becoming and Passing Away (decay), Support of the Living.
- **Three Pillars**: Active Basic Income, State Income, Equalisation and Environment Fund.
- **Triple Good**: Individual, Community, and Whole wellbeing.

When documenting a core function (e.g., `grdd_unit_calculate_decay`), you are free to say: *“Gradido’s second natural law – the cycle of becoming and passing away – guides this function.”* This signals to readers that they are looking at a central piece of the economic model.

**Even for core functions:** Do not preach. Describe the law, do not praise it.

### What to Avoid (Short List)

- Preaching (“should”, “must”, “good”, “fair”).
- Exclamation marks.
- Floating‑point illusions (always mention fixed‑point scaling where relevant).
- Redundant philosophy in helper functions (e.g., `grdd_unit_to_string` gets no natural law).
- Deleting or editing an existing `@whisper` unless the function changed completely.

### Enforcement & Maintenance

- When you generate or edit comments, prioritise **poetic precision** over dry correctness.
- This file is authoritative. When in doubt, follow these guidelines.

----------

**Remember:** The goal is not to produce perfect technical prose. The goal is to make reading the code a quiet pleasure – accurate, calm, and a little beautiful.
