cmake_minimum_required(VERSION 3.18.2)
cmake_policy(VERSION 3.18.2)
cmake_policy(SET CMP0135 NEW)

set(PROJECT_NAME gradido_blockchain_core)
project(${PROJECT_NAME} C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options for enabling benchmarks and tests (default OFF)
option(ENABLE_BENCHMARKS "Enable benchmark module" OFF)
option(ENABLE_TESTS "Enable test module" OFF)
option(USE_SODIUM "Enable use of libsodium" OFF)
option(USE_INSTALLED_SODIUM "Link against already installed sodium lib if USE_SODIUM was also set to On" OFF)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
)
if (USE_SODIUM)
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

FILE(GLOB DATA "src/data/*.c" "include/gradido_blockchain_core/data/*.h")
FILE(GLOB DATA_PROTO_GRADIDO "src/data/proto/gradido/*.c" "include/gradido_blockchain_core/data/proto/gradido/*.h")
FILE(GLOB DATA_RUNTIME "src/data/runtime/*.c" "include/gradido_blockchain_core/data/runtime/*.h")
FILE(GLOB DATA_WIRE "src/data/wire/*.c" "include/gradido_blockchain_core/data/wire/*.h")
FILE(GLOB MAPPING "src/mapping/*.c" "include/gradido_blockchain_core/mapping/*.h")
FILE(GLOB SRC "src/*.c" "include/gradido_blockchain_core/*.h")
FILE(GLOB TYPES "include/gradido_blockchain_core/types/*.h")
FILE(GLOB UTILS "src/utils/*.c" "include/gradido_blockchain_core/utils/*.h")

FILE(GLOB INTERACTION_VALIDATE
	"include/gradido_blockchain_core/interactions/validate/*.h"
	"src/interactions/validate/*.c"
)

set(GRADIDO_BLOCKCHAIN_CORE_SRC
  ${DATA}
  ${DATA_PROTO_GRADIDO}
  ${DATA_RUNTIME}
  ${DATA_WIRE}
  ${INTERACTION_VALIDATE}
  ${MAPPING}
  ${SRC}
  ${TYPES}
  ${UTILS}
)

# Third Party
FILE(GLOB FP256 "third_party/fp256/*.h" "third_party/fp256/src/*.h" "third_party/fp256/src/*.c")
FILE(GLOB PBTOOLS "third_party/pbtools/*.c" "third_party/pbtools/*.h")
FILE(GLOB R128 "third_party/r128/*.c" "third_party/r128/*.h")
set(GRADIDO_BLOCKCHAIN_CORE_THIRD_PARTY_SRC ${FP256} ${PBTOOLS} ${R128})

if(MSVC)
    source_group("data/proto/gradido" FILES ${DATA_PROTO_GRADIDO} )
    source_group("data/runtime" FILES  ${DATA_RUNTIME} )
    source_group("data/wire" FILES ${DATA_WIRE} )
    source_group("data" FILES ${DATA} )
    source_group("interactions/validate" FILES ${INTERACTION_VALIDATE})
    source_group("mapping" FILES ${MAPPING} )
    source_group("types" FILES ${TYPES})
    source_group("utils" FILES ${UTILS} )
    source_group("third_party/fp256" FILES ${FP256})
    source_group("third_party/pbtools" FILES ${PBTOOLS})
    source_group("third_party/r128" FILES ${R128})
endif()

include_directories(
    include
    include/gradido_blockchain_core/data/proto/gradido
    third_party
    third_party/pbtools
)

add_library(${PROJECT_NAME} STATIC ${GRADIDO_BLOCKCHAIN_CORE_SRC} ${GRADIDO_BLOCKCHAIN_CORE_THIRD_PARTY_SRC})
if (USE_SODIUM)
  target_link_libraries(${PROJECT_NAME} sodium)
  target_compile_definitions(${PROJECT_NAME} PRIVATE USE_SODIUM)
endif()

# enable debug symbols from parent project on visual studio on windows in debug build
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:/JMC>
)

if(ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()
