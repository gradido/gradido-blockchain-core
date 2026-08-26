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
