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

if (NOT USE_SODIUM)
  add_compile_definitions(USE_SODIUM)
  set(SODIUM_DISABLE_TESTS ON)
  if(USE_INSTALLED_SODIUM)
  	find_package(Sodium REQUIRED)
  	message(STATUS "Using installed Sodium library")
  else()
  	message(STATUS "Fetch Sodium library from GitHub")
  	# Update the commit to point to whatever libsodium-cmake-commit you want to target.
  	FetchContent_Declare(Sodium
  			GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
  			GIT_TAG e5b985ad0dd235d8c4307ea3a385b45e76c74c6a # HEAD, last updated at 2025-04-13
  	)
  	FetchContent_MakeAvailable(Sodium)
  endif()
  add_definitions(-DSODIUM_STATIC)
endif()

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# one binary per unit
add_executable(test_duration src/test_duration.cpp)
target_link_libraries(test_duration gradido_blockchain_core GTest::gtest_main)

add_executable(test_converter src/test_converter.cpp)
target_link_libraries(test_converter gradido_blockchain_core GTest::gtest_main sodium)

add_executable(test_crypto src/test_crypto.cpp src/utils.cpp)
target_link_libraries(test_crypto gradido_blockchain_core GTest::gtest_main sodium)

add_executable(test_data_wire src/test_data_wire.cpp)
target_link_libraries(test_data_wire gradido_blockchain_core GTest::gtest_main)

add_executable(test_memory src/test_memory.cpp)
target_link_libraries(test_memory gradido_blockchain_core GTest::gtest_main)

add_executable(test_pbtools src/test_pbtools.cpp src/key_pairs.cpp src/key_pairs.h terminal_colors.h)
target_link_libraries(test_pbtools gradido_blockchain_core GTest::gtest_main sodium)

add_executable(test_runtime src/test_runtime.cpp src/key_pairs.cpp src/key_pairs.h terminal_colors.h)
target_link_libraries(test_runtime gradido_blockchain_core GTest::gtest_main sodium)

add_executable(test_unit src/test_unit.cpp)
target_link_libraries(test_unit gradido_blockchain_core GTest::gtest_main)

enable_testing()

include(GoogleTest)
if(NOT CMAKE_CROSSCOMPILING OR (DEFINED CMAKE_CROSSCOMPILING_EMULATOR AND CMAKE_CROSSCOMPILING_EMULATOR))
  message(STATUS "Enable test_unit tests")
  add_test(NAME test_duration COMMAND test_duration)
  add_test(NAME test_converter COMMAND test_converter)
  add_test(NAME test_crypto COMMAND test_crypto)
  add_test(NAME test_data_wire COMMAND test_data_wire)
  add_test(NAME test_memory COMMAND test_memory)
  add_test(NAME test_pbtools COMMAND test_pbtools)
  add_test(NAME test_runtime COMMAND test_runtime)
  add_test(NAME test_unit COMMAND test_unit)
endif()
