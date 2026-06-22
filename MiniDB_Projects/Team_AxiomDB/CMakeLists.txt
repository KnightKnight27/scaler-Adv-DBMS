cmake_minimum_required(VERSION 3.16)
project(AxiomDB LANGUAGES CXX)

# ---------------------------------------------------------------------------
# AxiomDB build configuration
#
# A single-process relational mini-DBMS (LSM Storage Engine: LSM-tree storage extension).
# Standard library only -- no external DB engines, no serialization libs.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Warnings: a database is exactly the kind of code where -Wall pays for itself.
add_compile_options(-Wall -Wextra -Wpedantic)

# Optional sanitizers -- invaluable for catching the use-after-free / OOB-read
# bugs that lurk in hand-rolled page layouts.  Enable with -DAXIOMDB_ASAN=ON.
option(AXIOMDB_ASAN "Build with Address+UB sanitizers" OFF)
if(AXIOMDB_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Core engine library: everything under src/ except the CLI entry point.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE AXIOMDB_CORE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(FILTER AXIOMDB_CORE_SOURCES EXCLUDE REGEX "/src/cli/")

add_library(axiomdb_core STATIC ${AXIOMDB_CORE_SOURCES})
target_include_directories(axiomdb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(Threads REQUIRED)
target_link_libraries(axiomdb_core PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# CLI / REPL executable
# ---------------------------------------------------------------------------
file(GLOB CLI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/cli/*.cpp)
if(CLI_SOURCES)
  add_executable(axiomdb ${CLI_SOURCES})
  target_link_libraries(axiomdb PRIVATE axiomdb_core)
endif()

# ---------------------------------------------------------------------------
# Benchmark harness executable
# ---------------------------------------------------------------------------
file(GLOB BENCH_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp)
if(BENCH_SOURCES)
  add_executable(axiomdb_bench ${BENCH_SOURCES})
  target_link_libraries(axiomdb_bench PRIVATE axiomdb_core)
endif()

# ---------------------------------------------------------------------------
# Tests (Catch2 single-header, vendored under third_party/)
# ---------------------------------------------------------------------------
enable_testing()
add_library(catch2 INTERFACE)
target_include_directories(catch2 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/catch2)
target_compile_definitions(catch2 INTERFACE CATCH_CONFIG_FAST_COMPILE)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
if(TEST_SOURCES)
  add_executable(axiomdb_tests ${TEST_SOURCES})
  target_link_libraries(axiomdb_tests PRIVATE axiomdb_core catch2)
  add_test(NAME unit COMMAND axiomdb_tests)
endif()
