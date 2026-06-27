# Optional CMake build (the Makefile is the primary, zero-dependency build).
#   cmake -B build && cmake --build build
cmake_minimum_required(VERSION 3.13)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)
include_directories(${CMAKE_SOURCE_DIR}/src)

# Engine library = every .cpp under src/ except the shell entry point.
file(GLOB_RECURSE MINIDB_SRCS CONFIGURE_DEPENDS src/*.cpp)
list(FILTER MINIDB_SRCS EXCLUDE REGEX "src/main\\.cpp$")
add_library(minidb_engine STATIC ${MINIDB_SRCS})
target_link_libraries(minidb_engine PUBLIC Threads::Threads)

# SQL shell.
add_executable(minidb src/main.cpp)
target_link_libraries(minidb PRIVATE minidb_engine)

# Benchmark.
add_executable(bench benchmarks/bench.cpp)
target_link_libraries(bench PRIVATE minidb_engine)

# Tests.
enable_testing()
file(GLOB TEST_SRCS CONFIGURE_DEPENDS tests/*.cpp)
foreach(test_src ${TEST_SRCS})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE minidb_engine)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
