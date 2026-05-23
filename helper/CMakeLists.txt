cmake_minimum_required(VERSION 3.18.2)
cmake_policy(VERSION 3.18.2)

set(PROJECT_NAME precalculate_decay_table)
project(${PROJECT_NAME} C)

set(CMAKE_C_STANDARD 11)

add_executable(precalculate_decay_table precalculate_decay_table.c)
