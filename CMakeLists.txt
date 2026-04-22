cmake_minimum_required(VERSION 3.18.2)
cmake_policy(VERSION 3.18.2)

set(PROJECT_NAME gradido_blockchain_core)
project(${PROJECT_NAME} C)

set(CMAKE_C_STANDARD 11)

# Options for enabling benchmarks and tests (default OFF)
option(ENABLE_BENCHMARKS "Enable benchmark module" OFF)
option(ENABLE_TESTS "Enable test module" OFF)

FILE(GLOB DATA "src/data/*.c" "include/gradido_blockchain_core/data/*.h")
FILE(GLOB UTILS "src/utils/*.c" "include/gradido_blockchain_core/utils/*.h")

set(GRADIDO_BLOCKCHAIN_CORE_SRC ${DATA} ${UTILS})

if(MSVC)
  source_group("data" FILES ${DATA} )
  source_group("utils" FILES ${UTILS} )
endif()

include_directories(
    include
    third_party
)

add_library(${PROJECT_NAME} STATIC ${GRADIDO_BLOCKCHAIN_CORE_SRC})


if(ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()

