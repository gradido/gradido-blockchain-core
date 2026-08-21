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

add_executable(test_json src/test_json.cpp)
target_link_libraries(test_json gradido_blockchain_core GTest::gtest_main)

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
  if (USE_SODIUM)
    add_test(NAME test_converter COMMAND test_converter)
    add_test(NAME test_crypto COMMAND test_crypto)
    add_test(NAME test_pbtools COMMAND test_pbtools)
    add_test(NAME test_runtime COMMAND test_runtime)
  endif()
endif()
