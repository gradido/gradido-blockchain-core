cmake_minimum_required(VERSION 3.18.2)
cmake_policy(VERSION 3.18.2)

set(PROJECT_NAME precalculate_decay_table)
project(${PROJECT_NAME} C)

set(CMAKE_C_STANDARD 11)

include_directories(
  ../third_party
)

add_executable(precalculate_decay_table precalculate_decay_table.c ../third_party/fp256/src/fp256_mul.c ../third_party/fp256/src/fp256_utils.c)
