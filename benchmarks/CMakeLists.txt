cmake_minimum_required(VERSION 3.18.2)
cmake_policy(SET CMP0135 NEW)
project(GradidoBlockchainCoreBenchmarks C)

# one binary per unit
add_executable(bench_numberToString src/bench_numberToString.c)
target_link_libraries(bench_numberToString gradido_blockchain_core)

