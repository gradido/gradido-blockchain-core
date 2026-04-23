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
add_executable(test_unit src/test_unit.cpp)
target_link_libraries(test_unit gradido_blockchain_core GTest::gtest_main)

add_executable(test_duration src/test_duration.cpp)
target_link_libraries(test_duration gradido_blockchain_core GTest::gtest_main)

enable_testing()

include(GoogleTest)
if(NOT CMAKE_CROSSCOMPILING OR (DEFINED CMAKE_CROSSCOMPILING_EMULATOR AND CMAKE_CROSSCOMPILING_EMULATOR))
  message(STATUS "Enable test_unit tests")
  add_test(NAME test_unit COMMAND test_unit)
  add_test(NAME test_duration COMMAND test_duration)
endif()