# Vendored for benchmarks only

Nothing here is compiled into `gradido_blockchain_core`: `build.zig` walks only the top level
`third_party/` into the library. These are comparison points for `bench_tx_index_*` and the
prototype tests, not dependencies of the library. Do not edit; replace with a newer release.

| Directory | Upstream | Version | License |
|---|---|---|---|
| `croaring/` | https://github.com/RoaringBitmap/CRoaring | v4.6.1 amalgamation (`roaring.c`, `roaring.h` from the release assets) — same version `gradido_blockchain` fetches | Apache-2.0 / MIT |
| `stb/` | https://github.com/nothings/stb | `stb_ds.h` v0.67, commit 2c980bb59875b0d32144a71867fbdebb2f77cd20 | MIT / Public Domain |
