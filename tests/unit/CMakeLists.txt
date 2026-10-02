cmake_minimum_required(VERSION 3.18.2)
cmake_policy(SET CMP0135 NEW)
project(GradidoBlockchainCoreUnitTest C CXX)

# GoogleTest requires at least C++11
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# one binary per unit
add_executable(test_data src/test_data.cpp)
target_link_libraries(test_data gradido_blockchain_core GTest::gtest_main)

add_executable(test_data_wire src/test_data_wire.cpp)
target_link_libraries(test_data_wire gradido_blockchain_core GTest::gtest_main)

add_executable(test_unit src/test_unit.cpp)
target_link_libraries(test_unit gradido_blockchain_core GTest::gtest_main)

# The JSON mapping builds its fixtures rather than decoding them, so it needs no libsodium and
# stays with the three that run in every build.
add_executable(test_json src/test_json.cpp)
target_link_libraries(test_json gradido_blockchain_core GTest::gtest_main)

# The transaction index: every filter against a reference that walks the transactions.
# bench_chain_data.c comes along for the chain file loader and the community uuid it carries.
add_executable(test_index_transactions src/test_index_transactions.cpp
               ${CMAKE_SOURCE_DIR}/benchmarks/src/bench_chain_data.c
               ${CMAKE_SOURCE_DIR}/benchmarks/src/bench_chain_synth.c)
target_link_libraries(test_index_transactions gradido_blockchain_core GTest::gtest_main)
target_include_directories(test_index_transactions PRIVATE ${CMAKE_SOURCE_DIR}/benchmarks/src)
if (USE_SODIUM)
  target_link_libraries(test_index_transactions sodium)
endif()

add_executable(test_chain_synth src/test_chain_synth.cpp
               ${CMAKE_SOURCE_DIR}/benchmarks/src/bench_chain_synth.c)
target_link_libraries(test_chain_synth gradido_blockchain_core GTest::gtest_main)
target_include_directories(test_chain_synth PRIVATE ${CMAKE_SOURCE_DIR}/benchmarks/src)
if (USE_SODIUM)
  target_link_libraries(test_chain_synth sodium)
endif()

add_executable(test_index_transactions_filter src/test_index_transactions_filter.cpp)
target_link_libraries(test_index_transactions_filter gradido_blockchain_core GTest::gtest_main)
if (USE_SODIUM)
  target_link_libraries(test_index_transactions_filter sodium)
endif()

add_executable(test_index_addresses src/test_index_addresses.cpp
               ${CMAKE_SOURCE_DIR}/benchmarks/src/bench_chain_data.c
               ${CMAKE_SOURCE_DIR}/benchmarks/src/bench_chain_synth.c)
target_link_libraries(test_index_addresses gradido_blockchain_core GTest::gtest_main)
target_include_directories(test_index_addresses PRIVATE ${CMAKE_SOURCE_DIR}/benchmarks/src)
if (USE_SODIUM)
  target_link_libraries(test_index_addresses sodium)
endif()

# The store behind a chain: in memory, and a host answering across an FFI boundary.
add_executable(test_blockchain_store src/test_blockchain_store.cpp)
target_link_libraries(test_blockchain_store gradido_blockchain_core GTest::gtest_main)
if (USE_SODIUM)
  target_link_libraries(test_blockchain_store sodium)
endif()

# The chain over those indices: the same answers however the row of segments is cut.
add_executable(test_blockchain_chain src/test_blockchain_chain.cpp
               ${CMAKE_SOURCE_DIR}/benchmarks/src/bench_chain_data.c
               ${CMAKE_SOURCE_DIR}/benchmarks/src/bench_chain_synth.c)
target_link_libraries(test_blockchain_chain gradido_blockchain_core GTest::gtest_main)
target_include_directories(test_blockchain_chain PRIVATE ${CMAKE_SOURCE_DIR}/benchmarks/src)
if (USE_SODIUM)
  target_link_libraries(test_blockchain_chain sodium)
endif()

# The map prototypes behind bench_tx_index_map, every variant against a reference.
add_executable(test_tx_index_proto src/test_tx_index_proto.cpp)
target_link_libraries(test_tx_index_proto tx_index_proto GTest::gtest_main)

# arnm/roaring_bitmap.h against CRoaring, on real chain data and a synthetic chain.
add_executable(test_tx_index_roaring src/test_tx_index_roaring.cpp
               ${CMAKE_SOURCE_DIR}/benchmarks/src/bench_chain_data.c)
target_link_libraries(test_tx_index_roaring tx_index_proto tx_index_croaring_default GTest::gtest_main)

foreach(target test_tx_index_proto test_tx_index_roaring)
  if (USE_SODIUM)
    target_link_libraries(${target} sodium)
  endif()
endforeach()

# Without USE_SODIUM the crypto sources are not part of gradido_blockchain_core at all, so a
# binary that reaches into them -- directly, or through the key pairs it signs its fixtures
# with -- has nothing to link against. These four are declared only when it is there, which is
# the same line build.zig draws; see AGENTS.md. Sodium itself is fetched by the top level under
# the same condition, and its definitions reach this directory from there.
if (USE_SODIUM)
  add_executable(test_converter src/test_converter.cpp)
  target_link_libraries(test_converter gradido_blockchain_core GTest::gtest_main sodium)

  add_executable(test_crypto src/test_crypto.cpp src/utils.cpp)
  target_link_libraries(test_crypto gradido_blockchain_core GTest::gtest_main sodium)

  add_executable(test_pbtools src/test_pbtools.cpp src/key_pairs.cpp src/key_pairs.h terminal_colors.h)
  target_link_libraries(test_pbtools gradido_blockchain_core GTest::gtest_main sodium)

  add_executable(test_runtime src/test_runtime.cpp src/key_pairs.cpp src/key_pairs.h terminal_colors.h)
  target_link_libraries(test_runtime gradido_blockchain_core GTest::gtest_main sodium)
endif()

enable_testing()

include(GoogleTest)
if(NOT CMAKE_CROSSCOMPILING OR (DEFINED CMAKE_CROSSCOMPILING_EMULATOR AND CMAKE_CROSSCOMPILING_EMULATOR))
  message(STATUS "Enable test_unit tests")
  add_test(NAME test_data COMMAND test_data)
  add_test(NAME test_data_wire COMMAND test_data_wire)
  add_test(NAME test_unit COMMAND test_unit)
  add_test(NAME test_json COMMAND test_json)
  add_test(NAME test_index_addresses COMMAND test_index_addresses)
  add_test(NAME test_chain_synth COMMAND test_chain_synth)
  add_test(NAME test_index_transactions_filter COMMAND test_index_transactions_filter)
  add_test(NAME test_index_transactions COMMAND test_index_transactions)
  add_test(NAME test_blockchain_chain COMMAND test_blockchain_chain)
  add_test(NAME test_blockchain_store COMMAND test_blockchain_store)
  add_test(NAME test_tx_index_proto COMMAND test_tx_index_proto)
  add_test(NAME test_tx_index_roaring COMMAND test_tx_index_roaring)
  if (USE_SODIUM)
    add_test(NAME test_converter COMMAND test_converter)
    add_test(NAME test_crypto COMMAND test_crypto)
    add_test(NAME test_pbtools COMMAND test_pbtools)
    add_test(NAME test_runtime COMMAND test_runtime)
  endif()
endif()
