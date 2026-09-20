cmake_minimum_required(VERSION 3.18.2)
cmake_policy(SET CMP0135 NEW)
project(GradidoBlockchainCoreBenchmarks C)

# one binary per unit
add_executable(bench_numberToString src/bench_numberToString.c)
target_compile_options(bench_numberToString PRIVATE ${GRADIDO_WARNING_FLAGS})
target_link_libraries(bench_numberToString gradido_blockchain_core)
# This one needs no crypto of its own, but the library it links carries sodium calls in its
# converter, so it follows whatever the library was built with rather than naming sodium
# unconditionally -- without USE_SODIUM there is no such target to name.
if (USE_SODIUM)
  target_link_libraries(bench_numberToString sodium)
endif()
if(NOT WIN32)
  target_link_libraries(bench_numberToString m)
endif()

# one binary per unit
add_executable(bench_json src/bench_json.c)
target_compile_options(bench_json PRIVATE ${GRADIDO_WARNING_FLAGS})
target_link_libraries(bench_json gradido_blockchain_core)
# The JSON mapping needs no crypto of its own -- binary travels as hex and base64, both of which
# arnm carries. The
# library it links may still hold sodium calls elsewhere, so it follows what that was built with
# rather than naming sodium unconditionally.
if (USE_SODIUM)
  target_link_libraries(bench_json sodium)
endif()
if(NOT WIN32)
  target_link_libraries(bench_json m)
endif()

if (USE_SODIUM)
  # one binary per unit
  add_executable(bench_crypto src/bench_crypto.c)
  target_compile_options(bench_crypto PRIVATE ${GRADIDO_WARNING_FLAGS})
  target_link_libraries(bench_crypto gradido_blockchain_core sodium)
  if(NOT WIN32)
    target_link_libraries(bench_crypto m)
  endif()

  # one binary per unit
  # Puts arnm's base64 -- the pair the JSON mapping uses -- next to libsodium's. Nothing of the
  # mapping needs sodium; this needs it to have something to compare against.
  add_executable(bench_base64 src/bench_base64.c)
  target_compile_options(bench_base64 PRIVATE ${GRADIDO_WARNING_FLAGS})
  target_link_libraries(bench_base64 gradido_blockchain_core sodium)
  if(NOT WIN32)
    target_link_libraries(bench_base64 m)
  endif()
endif()

# Transaction index prototypes (see tx_index_proto.cmake). One benchmark body per bitmap
# backend, so every row compares with the same row of the others. The chain file is optional:
# pass it as the first argument or in GRD_BENCH_CHAIN_DATA.
add_executable(bench_tx_index_map src/bench_tx_index_map.c src/bench_chain_data.c)
target_compile_options(bench_tx_index_map PRIVATE ${GRADIDO_WARNING_FLAGS})
target_link_libraries(bench_tx_index_map tx_index_proto)

# One binary over three backends: arnm's sets, CRoaring's 32 bit sets and its roaring64. Built
# without x64 SIMD, the way the CPU this index is for runs it.
add_executable(bench_tx_index_bitmap
  src/bench_tx_index_bitmap.c
  src/bench_bitmap_arnm.c
  src/bench_bitmap_croaring32.c
  src/bench_bitmap_croaring64.c
  src/bench_chain_data.c
)
target_compile_options(bench_tx_index_bitmap PRIVATE ${GRADIDO_WARNING_FLAGS})
target_compile_definitions(bench_tx_index_bitmap PRIVATE ROARING_DISABLE_X64)
target_link_libraries(bench_tx_index_bitmap tx_index_proto tx_index_croaring_scalar)

add_executable(bench_index_addresses src/bench_index_addresses.c src/bench_chain_synth.c)
target_compile_options(bench_index_addresses PRIVATE ${GRADIDO_WARNING_FLAGS})
target_link_libraries(bench_index_addresses gradido_blockchain_core)
target_include_directories(bench_index_addresses PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(bench_index_transactions src/bench_index_transactions.c src/bench_chain_synth.c)
target_compile_options(bench_index_transactions PRIVATE ${GRADIDO_WARNING_FLAGS})
target_link_libraries(bench_index_transactions gradido_blockchain_core)
target_include_directories(bench_index_transactions PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(bench_tx_index_layout src/bench_tx_index_layout.c src/bench_chain_data.c)
target_compile_options(bench_tx_index_layout PRIVATE ${GRADIDO_WARNING_FLAGS})
target_link_libraries(bench_tx_index_layout tx_index_proto)

foreach(target bench_tx_index_map bench_tx_index_bitmap bench_tx_index_layout
               bench_index_transactions bench_index_addresses)
  if (USE_SODIUM)
    target_link_libraries(${target} sodium)
  endif()
  if(NOT WIN32)
    target_link_libraries(${target} m)
  endif()
endforeach()
