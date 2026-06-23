cmake_minimum_required(VERSION 3.17)
project(MiniDB VERSION 1.0 LANGUAGES CXX)

# ── C++17 standard, required for std::optional, std::variant, structured bindings ──
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ──
add_compile_options(-Wall -Wextra -Wpedantic)

# ── Source files shared by all targets ──
set(MINIDB_SOURCES
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

# ── Include directories ──
include_directories(src)

# ── Main MiniDB executable ──
add_executable(minidb
    src/main.cpp
    ${MINIDB_SOURCES}
)
target_link_libraries(minidb pthread)

# ── Benchmark: insert latency ──
add_executable(bench_insert
    benchmarks/bench_insert.cpp
    ${MINIDB_SOURCES}
)
target_link_libraries(bench_insert pthread)

# ── Benchmark: select / scan vs index ──
add_executable(bench_select
    benchmarks/bench_select.cpp
    ${MINIDB_SOURCES}
)
target_link_libraries(bench_select pthread)

# ── Build output directory ──
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
