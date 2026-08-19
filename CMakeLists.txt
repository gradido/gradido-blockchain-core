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
option(ENABLE_SANITIZERS "Build with AddressSanitizer, plus UndefinedBehaviorSanitizer on non-MSVC toolchains (MSVC ships no UBSan)" OFF)
option(ENABLE_THREAD_SANITIZER "Build with ThreadSanitizer (mutually exclusive with ENABLE_SANITIZERS)" OFF)

# Sanitizers are set before any target is declared so that the library, the third party
# sources, the tests and the benchmarks are all instrumented the same way — mixing an
# instrumented library with an uninstrumented test binary produces false reports.
if(ENABLE_SANITIZERS AND ENABLE_THREAD_SANITIZER)
  message(FATAL_ERROR "ENABLE_SANITIZERS and ENABLE_THREAD_SANITIZER cannot be combined; ThreadSanitizer does not run together with AddressSanitizer")
endif()

if(ENABLE_SANITIZERS)
  if(MSVC)
    # MSVC ships AddressSanitizer only, there is no UndefinedBehaviorSanitizer counterpart
    message(STATUS "Sanitizers enabled: address")
    add_compile_options(/fsanitize=address /Zi)
  else()
    message(STATUS "Sanitizers enabled: address, undefined")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

if(ENABLE_THREAD_SANITIZER)
  if(MSVC)
    message(FATAL_ERROR "ENABLE_THREAD_SANITIZER is not supported by MSVC")
  endif()
  message(STATUS "Sanitizers enabled: thread")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=thread)
endif()

include(FetchContent)

# hostmem carries the allocator, the containers and the number conversions this project used to
# keep in src/utils. Its sources join this library's target below rather than linking a second
# archive, which is also what keeps the sanitizer flags set above on every byte of it.
#
# Since v0.3.0 hostmem ships a CMakeLists of its own. FetchContent_MakeAvailable would call
# add_subdirectory() on it, and because our ENABLE_TESTS / ENABLE_BENCHMARKS options carry the
# same names as its own, -DENABLE_TESTS=ON here would build hostmem's googletest suite too.
# SOURCE_SUBDIR names a directory that holds no CMakeLists, which is the documented way to say
# populate only. Point it at the real thing instead if this project ever wants that target.
FetchContent_Declare(hostmem
  URL https://github.com/einhornimmond/hostmem/archive/refs/tags/v0.3.1.tar.gz
  # Pins what a tag resolves to. A tag can be moved and GitHub regenerates its archives, so
  # without this the build takes whatever the URL serves today. Update it with the version:
  #   curl -sSL <url> | sha256sum
  URL_HASH SHA256=24fdafd560e2e570fdf10ee9823b4b67b5736fb49136038948391b12898a0032
  SOURCE_SUBDIR do-not-add-subdirectory
)
FetchContent_MakeAvailable(hostmem)
FILE(GLOB_RECURSE HOSTMEM_SRC "${hostmem_SOURCE_DIR}/src/*.c" "${hostmem_SOURCE_DIR}/include/hostmem/*.h")
message(STATUS "hostmem from ${hostmem_SOURCE_DIR}")

if (USE_SODIUM)
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

if (USE_SODIUM)
FILE(GLOB CRYPTO "src/crypto/*.c" "include/gradido_blockchain_core/crypto/*.h")
endif()
FILE(GLOB DATA "src/data/*.c" "include/gradido_blockchain_core/data/*.h")
FILE(GLOB DATA_PROTO_GRADIDO "src/data/proto/gradido/*.c" "include/gradido_blockchain_core/data/proto/gradido/*.h")
FILE(GLOB DATA_RUNTIME "src/data/runtime/*.c" "include/gradido_blockchain_core/data/runtime/*.h")
FILE(GLOB DATA_WIRE "src/data/wire/*.c" "include/gradido_blockchain_core/data/wire/*.h")
FILE(GLOB MAPPING "src/mapping/*.c" "include/gradido_blockchain_core/mapping/*.h")
FILE(GLOB SRC "src/*.c" "include/gradido_blockchain_core/*.h")
FILE(GLOB TYPES "src/types/*.c" "include/gradido_blockchain_core/types/*.h")
FILE(GLOB UTILS "src/utils/*.c" "include/gradido_blockchain_core/utils/*.h")

FILE(GLOB INTERACTION_VALIDATE
	"include/gradido_blockchain_core/interactions/validate/*.h"
	"src/interactions/validate/*.c"
)

set(GRADIDO_BLOCKCHAIN_CORE_SRC
  ${HOSTMEM_SRC}
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

if (USE_SODIUM)
set(GRADIDO_BLOCKCHAIN_CORE_SRC ${GRADIDO_BLOCKCHAIN_CORE_SRC} ${CRYPTO})
endif()

# Third Party
FILE(GLOB FP256 "third_party/fp256/*.h" "third_party/fp256/src/*.h" "third_party/fp256/src/*.c")
FILE(GLOB PBTOOLS "third_party/pbtools/*.c" "third_party/pbtools/*.h")
FILE(GLOB R128 "third_party/r128/*.c" "third_party/r128/*.h")
set(GRADIDO_BLOCKCHAIN_CORE_THIRD_PARTY_SRC ${FP256} ${PBTOOLS} ${R128})

if(MSVC)
if (USE_SODIUM)
    source_group("crypto" FILES ${CRYPTO})
endif()
    source_group("data/proto/gradido" FILES ${DATA_PROTO_GRADIDO} )
    source_group("data/runtime" FILES  ${DATA_RUNTIME} )
    source_group("data/wire" FILES ${DATA_WIRE} )
    source_group("data" FILES ${DATA} )
    source_group("interactions/validate" FILES ${INTERACTION_VALIDATE})
    source_group("mapping" FILES ${MAPPING} )
    source_group("types" FILES ${TYPES})
    source_group("utils" FILES ${UTILS} )
    source_group("hostmem" FILES ${HOSTMEM_SRC})
    source_group("third_party/fp256" FILES ${FP256})
    source_group("third_party/pbtools" FILES ${PBTOOLS})
    source_group("third_party/r128" FILES ${R128})
endif()

include_directories(
    include
    ${hostmem_SOURCE_DIR}/include
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
# /JMC is an MSVC flag; gcc and clang reject it as an unknown linker input
if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE
      $<$<CONFIG:Debug>:/JMC>
  )
endif()

if(ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(ENABLE_TESTS)
    # Called here and not only down in tests/unit: ctest reads the CTestTestfile.cmake of the
    # directory it is pointed at, and a top level that never enabled testing writes one that
    # does not descend. Without this `ctest --test-dir <build>` answers "No tests were found"
    # while the binaries sit one level down, and only `--test-dir <build>/tests/unit` works.
    enable_testing()
    add_subdirectory(tests)
endif()
