# ============================================================================
# CMakeLists.txt  --  CMake build for MiniDB (same toolchain as the course repo).
#
#   cmake -S . -B build
#   cmake --build build
#
# Produces three executables in build/ :
#   minidb           the SQL REPL
#   bench            the LSM vs B+ Tree benchmark
#   demo_concurrency the 2PL locking + deadlock demo
#
# (A plain `make` using the top-level Makefile also works, for convenience.)
# ============================================================================
cmake_minimum_required(VERSION 3.10)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)
# We use `using namespace std;`, so move()/forward() are unqualified by design.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wno-unqualified-std-cast-call)
endif()

include_directories(${CMAKE_SOURCE_DIR}/src)

# The engine library = every .cpp under src/ except the three entry points.
file(GLOB_RECURSE LIB_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM LIB_SOURCES
  ${CMAKE_SOURCE_DIR}/src/main.cpp
  ${CMAKE_SOURCE_DIR}/src/bench.cpp
  ${CMAKE_SOURCE_DIR}/src/demo_concurrency.cpp)

add_library(minidb_core STATIC ${LIB_SOURCES})
find_package(Threads REQUIRED)
target_link_libraries(minidb_core PUBLIC Threads::Threads)

add_executable(minidb src/main.cpp)
target_link_libraries(minidb PRIVATE minidb_core)

add_executable(bench src/bench.cpp)
target_link_libraries(bench PRIVATE minidb_core)

add_executable(demo_concurrency src/demo_concurrency.cpp)
target_link_libraries(demo_concurrency PRIVATE minidb_core)
