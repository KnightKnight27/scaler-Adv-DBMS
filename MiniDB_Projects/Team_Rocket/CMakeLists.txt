cmake_minimum_required(VERSION 3.15)
project(minidb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# The engine is header-only; these targets are just the entry points.
add_executable(minidb src/main.cpp)
add_executable(minidb_tests src/tests.cpp)
add_executable(bench_columnar benchmarks/bench_columnar.cpp)

target_include_directories(minidb_tests PRIVATE src)

enable_testing()
add_test(NAME unit_tests COMMAND minidb_tests)
