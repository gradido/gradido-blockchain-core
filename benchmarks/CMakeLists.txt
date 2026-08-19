cmake_minimum_required(VERSION 3.18.2)
cmake_policy(SET CMP0135 NEW)
project(GradidoBlockchainCoreBenchmarks C)

# one binary per unit
add_executable(bench_numberToString src/bench_numberToString.c)
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

if (USE_SODIUM)
  # one binary per unit
  add_executable(bench_crypto src/bench_crypto.c)
  target_link_libraries(bench_crypto gradido_blockchain_core sodium)
  if(NOT WIN32)
    target_link_libraries(bench_crypto m)
  endif()
endif()
