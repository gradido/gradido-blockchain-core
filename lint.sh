#!/bin/bash
clang-format -i src/*.c
clang-format -i src/**/*.c
clang-format -i src/**/**/*.c
clang-format -i include/**/*.h
clang-format -i include/**/**/*.h
clang-format -i include/**/**/**/*.h
clang-format -i tests/unit/src/*.cpp
clang-format -i benchmarks/src/*.c
clang-format -i benchmarks/src/*.h
