cmake_minimum_required(VERSION 3.17)
project(MiniDB VERSION 1.0 LANGUAGES CXX)

# --- C++17 Standard Requirements ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Common Database Engine Sources ---
set(MINIDB_CORE_SOURCES
    src/storage/disk_manager.cpp
    src/storage/buffer_pool.cpp
    src/storage/heap_file.cpp
    src/index/bplus_tree.cpp
    src/query/parser.cpp
    src/query/executor.cpp
    src/query/optimizer.cpp
    src/transaction/lock_manager.cpp
    src/transaction/tx_manager.cpp
    src/recovery/wal.cpp
    src/recovery/recovery.cpp
    src/replication/primary.cpp
    src/replication/replica.cpp
)

# --- Main CLI Shell Executable ---
add_executable(minidb
    src/main.cpp
    ${MINIDB_CORE_SOURCES}
)
target_include_directories(minidb PRIVATE src)
target_compile_options(minidb PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(minidb PRIVATE pthread)

# --- Benchmark: Insert Latency ---
add_executable(bench_insert
    benchmarks/bench_insert.cpp
    ${MINIDB_CORE_SOURCES}
)
target_include_directories(bench_insert PRIVATE src)
target_compile_options(bench_insert PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(bench_insert PRIVATE pthread)

# --- Benchmark: Select / Scan vs Index ---
add_executable(bench_select
    benchmarks/bench_select.cpp
    ${MINIDB_CORE_SOURCES}
)
target_include_directories(bench_select PRIVATE src)
target_compile_options(bench_select PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(bench_select PRIVATE pthread)
