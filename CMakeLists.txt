cmake_minimum_required(VERSION 3.18.2)
cmake_policy(VERSION 3.18.2)

set(PROJECT_NAME gradido_blockchain_core)
project(${PROJECT_NAME} C)

set(CMAKE_C_STANDARD 11)

# Options for enabling benchmarks and tests (default OFF)
option(ENABLE_BENCHMARKS "Enable benchmark module" OFF)
option(ENABLE_TESTS "Enable test module" OFF)

FILE(GLOB DATA "src/data/*.c" "include/gradido_blockchain_core/data/*.h")
FILE(GLOB DATA_PROTO_GRADIDO "src/data/proto/gradido/*.c" "include/gradido_blockchain_core/data/proto/gradido/*.h")
FILE(GLOB DATA_WIRE "src/data/wire/*.c" "include/gradido_blockchain_core/data/wire/*.h")
FILE(GLOB MAPPING "src/mapping/*.c" "include/gradido_blockchain_core/mapping/*.h")
FILE(GLOB SRC "src/*.c" "include/gradido_blockchain_core/*.h")
FILE(GLOB TYPES "include/gradido_blockchain_core/types/*.h")
FILE(GLOB UTILS "src/utils/*.c" "include/gradido_blockchain_core/utils/*.h")

set(GRADIDO_BLOCKCHAIN_CORE_SRC ${DATA} ${DATA_PROTO_GRADIDO} ${DATA_WIRE} ${MAPPING} ${SRC} ${TYPES} ${UTILS})

# Third Party
FILE(GLOB FP256 "third_party/fp256/*.h" "third_party/fp256/src/*.h" "third_party/fp256/src/*.c")
FILE(GLOB PBTOOLS "third_party/pbtools/*.c" "third_party/pbtools/*.h")
FILE(GLOB R128 "third_party/r128/*.c" "third_party/r128/*.h")
set(GRADIDO_BLOCKCHAIN_CORE_THIRD_PARTY_SRC ${FP256} ${PBTOOLS} ${R128})

if(MSVC)
    source_group("data/proto/gradido" FILES ${DATA_PROTO_GRADIDO} )
    source_group("data/wire" FILES ${DATA_WIRE} )
    source_group("data" FILES ${DATA} )
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
