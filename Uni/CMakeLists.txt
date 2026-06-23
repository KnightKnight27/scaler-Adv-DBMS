cmake_minimum_required(VERSION 3.15)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compile flags for g++
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -O2 -pthread)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Source files for the core engine
set(CORE_SOURCES
    src/storage/disk.cpp
    src/storage/buffer.cpp
    src/index/btree.cpp
    src/query/parser.cpp
    src/query/operators.cpp
    src/query/optimizer.cpp
    src/concurrency/lock_manager.cpp
    src/concurrency/mvcc.cpp
    src/recovery/log_manager.cpp
    src/recovery/recovery_manager.cpp
    src/db.cpp
)

# Executable for interactive CLI & demo
add_executable(minidb src/main.cpp ${CORE_SOURCES})
target_link_libraries(minidb PRIVATE -pthread)

# Executable for benchmarking
add_executable(minidb_benchmark benchmarks/benchmark.cpp ${CORE_SOURCES})
target_link_libraries(minidb_benchmark PRIVATE -pthread)
