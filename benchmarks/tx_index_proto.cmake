# Transaction index prototypes: the maps under benchmarks/src/proto, and the vendored libraries
# they are measured against (benchmarks/third_party). Shared by the bench_tx_index_* binaries,
# test_tx_index_proto and test_tx_index_roaring, so it is included from the top level whenever
# either is enabled. Mirrors the tx_index targets in build.zig.

set(TX_INDEX_PROTO_DIR "${CMAKE_CURRENT_LIST_DIR}")

add_library(tx_index_proto STATIC
  ${TX_INDEX_PROTO_DIR}/src/proto/counted_alloc.c
  ${TX_INDEX_PROTO_DIR}/src/proto/map_lp_inline.c
  ${TX_INDEX_PROTO_DIR}/src/proto/map_lp_narrow.c
  ${TX_INDEX_PROTO_DIR}/src/proto/map_sorted.c
  ${TX_INDEX_PROTO_DIR}/src/proto/map_stb.c
)
target_include_directories(tx_index_proto PUBLIC ${TX_INDEX_PROTO_DIR}/src)
target_include_directories(tx_index_proto SYSTEM PUBLIC ${TX_INDEX_PROTO_DIR}/third_party)
target_link_libraries(tx_index_proto PUBLIC gradido_blockchain_core)
target_compile_options(tx_index_proto PRIVATE ${GRADIDO_WARNING_FLAGS})
# map_stb.c expands stb_ds: not our code to hold to the warning flags, and stb_ds v0.67 shifts
# into the sign bit in its siphash (stb_ds.h:1082), which UBSan reports on every other key
set(TX_INDEX_STB_OPTIONS ${GRADIDO_NO_WARNING_FLAGS})
if(ENABLE_SANITIZERS AND NOT MSVC)
  list(APPEND TX_INDEX_STB_OPTIONS -fno-sanitize=undefined)
endif()
set_source_files_properties(${TX_INDEX_PROTO_DIR}/src/proto/map_stb.c
  PROPERTIES COMPILE_OPTIONS "${TX_INDEX_STB_OPTIONS}")

# CRoaring twice: as it builds by default (x64 SIMD chosen at run time, AVX-512 off as
# gradido_blockchain has it) and with every x64 SIMD path compiled out. The definitions are
# PUBLIC because roaring.h reads them too.
foreach(variant default scalar)
  set(target tx_index_croaring_${variant})
  add_library(${target} STATIC ${TX_INDEX_PROTO_DIR}/third_party/croaring/roaring.c)
  target_include_directories(${target} SYSTEM PUBLIC ${TX_INDEX_PROTO_DIR}/third_party)
  target_compile_definitions(${target} PUBLIC CROARING_COMPILER_SUPPORTS_AVX512=0)
  target_compile_options(${target} PRIVATE ${GRADIDO_NO_WARNING_FLAGS})
  if(variant STREQUAL "scalar")
    target_compile_definitions(${target} PUBLIC ROARING_DISABLE_X64)
  endif()
endforeach()
