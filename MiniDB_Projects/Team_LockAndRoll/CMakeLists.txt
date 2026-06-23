cmake_minimum_required(VERSION 3.16)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

find_package(Threads REQUIRED)

# Core engine library: every src/*.cpp except entrypoints.
file(GLOB MINIDB_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER MINIDB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

add_library(minidb STATIC ${MINIDB_SOURCES})
target_include_directories(minidb PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(minidb PUBLIC Threads::Threads)

# Interactive CLI / REPL.
add_executable(minidb_cli src/main.cpp)
target_link_libraries(minidb_cli PRIVATE minidb)

# Test runner.
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*.cpp")
add_executable(minidb_tests ${TEST_SOURCES})
target_link_libraries(minidb_tests PRIVATE minidb)

# Benchmark driver.
add_executable(minidb_bench benchmarks/bench.cpp)
target_link_libraries(minidb_bench PRIVATE minidb)

enable_testing()
add_test(NAME unit_tests COMMAND minidb_tests)
