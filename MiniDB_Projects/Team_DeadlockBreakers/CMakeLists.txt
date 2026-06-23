cmake_minimum_required(VERSION 3.16)
project(MiniDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Source files ──────────────────────────────────────────────
set(MINIDB_SOURCES
    src/storage/page.cpp
    src/storage/disk_manager.cpp
    src/storage/buffer_pool.cpp
    src/storage/heap_file.cpp
    src/index/bplus_tree.cpp
    src/catalog/catalog.cpp
    src/execution/parser.cpp
    src/execution/statement_parser.cpp
    src/execution/value_compare.cpp
    src/execution/scan_operators.cpp
    src/execution/filter_operator.cpp
    src/execution/join_operator.cpp
    src/execution/projection_operator.cpp
    src/execution/row_operators.cpp
    src/execution/executor.cpp
    src/optimizer/optimizer.cpp
    src/transaction/lock_manager.cpp
    src/transaction/transaction.cpp
    src/transaction/txn_manager.cpp
    src/recovery/wal.cpp
    src/recovery/recovery_manager.cpp
    src/demo/demo_runner.cpp
)

# ── MiniDB static library (shared by main + benchmark) ──────
add_library(minidb_lib STATIC ${MINIDB_SOURCES})
target_include_directories(minidb_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)

# ── Main executable ─────────────────────────────────────────
add_executable(minidb src/main.cpp)
target_link_libraries(minidb PRIVATE minidb_lib)

# ── Benchmark executable ────────────────────────────────────
add_executable(benchmark
    benchmarks/benchmark.cpp
    benchmarks/benchmark_sources.cpp
    benchmarks/benchmark_runner.cpp
)
target_include_directories(benchmark PRIVATE ${CMAKE_SOURCE_DIR}/benchmarks)
target_link_libraries(benchmark PRIVATE minidb_lib)

enable_testing()
add_executable(minidb_tests
    tests/test_main.cpp
    tests/storage_test.cpp
    tests/transaction_test.cpp
    tests/recovery_test.cpp
)
target_include_directories(minidb_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(minidb_tests PRIVATE minidb_lib)
add_test(NAME minidb_correctness COMMAND minidb_tests)

# ── Platform-specific threading ─────────────────────────────
find_package(Threads REQUIRED)
target_link_libraries(minidb_lib PUBLIC Threads::Threads)
