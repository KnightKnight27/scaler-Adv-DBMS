cmake_minimum_required(VERSION 3.14)
project(MiniDB VERSION 1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# All source files resolved from src/ root
include_directories(${CMAKE_SOURCE_DIR}/src)

# ─────────────────────────────────────────────────────────────────────────────
# Core library (everything except main)
# ─────────────────────────────────────────────────────────────────────────────
set(MINIDB_SOURCES
    src/storage/disk_manager.cpp
    src/storage/buffer_pool.cpp
    src/storage/heap_file.cpp
    src/index/bplus_tree.cpp
    src/query/parser.cpp
    src/query/optimizer.cpp
    src/query/executor.cpp
    src/transaction/lock_manager.cpp
    src/transaction/tx_manager.cpp
    src/recovery/wal.cpp
    src/recovery/recovery.cpp
    src/replication/primary.cpp
    src/replication/replica.cpp
)

add_library(minidb_core STATIC ${MINIDB_SOURCES})
target_link_libraries(minidb_core Threads::Threads)

# ─────────────────────────────────────────────────────────────────────────────
# Main executable
# ─────────────────────────────────────────────────────────────────────────────
add_executable(minidb src/main.cpp)
target_link_libraries(minidb minidb_core)

# ─────────────────────────────────────────────────────────────────────────────
# Benchmarks
# ─────────────────────────────────────────────────────────────────────────────
add_executable(bench_insert benchmarks/bench_insert.cpp)
target_link_libraries(bench_insert minidb_core)

add_executable(bench_select benchmarks/bench_select.cpp)
target_link_libraries(bench_select minidb_core)
