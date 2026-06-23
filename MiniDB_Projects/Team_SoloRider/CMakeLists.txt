cmake_minimum_required(VERSION 3.16)
project(MiniDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings for clean code
add_compile_options(-Wall -Wextra -Wpedantic)

# Thread support (needed for transactions / concurrency)
find_package(Threads REQUIRED)

# ─── Source include path ────────────────────────────────────────
include_directories(${CMAKE_SOURCE_DIR}/src)

# ─── Storage module ────────────────────────────────────────────
add_library(minidb_storage
    src/storage/page.cpp
    src/storage/heap_file.cpp
    src/storage/buffer_pool.cpp
)
target_link_libraries(minidb_storage Threads::Threads)

# ─── Catalog module ────────────────────────────────────────────
add_library(minidb_catalog
    src/catalog/catalog.cpp
)
target_link_libraries(minidb_catalog minidb_storage)

# ─── Index module ──────────────────────────────────────────────
add_library(minidb_index
    src/index/bplus_tree.cpp
)
target_link_libraries(minidb_index minidb_storage minidb_catalog)

# ─── SQL parsing module ───────────────────────────────────────
add_library(minidb_sql
    src/sql/lexer.cpp
    src/sql/parser.cpp
)

# ─── Plan module ───────────────────────────────────────────────
add_library(minidb_plan
    src/plan/logical_plan.cpp
    src/plan/optimizer.cpp
    src/plan/physical_plan.cpp
)

# ─── Execution module ─────────────────────────────────────────
add_library(minidb_exec
    src/exec/seq_scan.cpp
    src/exec/index_scan.cpp
    src/exec/filter.cpp
    src/exec/projection.cpp
    src/exec/nested_loop_join.cpp
)
target_link_libraries(minidb_exec minidb_storage minidb_catalog minidb_index minidb_sql)
target_link_libraries(minidb_plan minidb_sql minidb_catalog minidb_index minidb_storage minidb_exec)

# ─── Transaction module ───────────────────────────────────────
add_library(minidb_txn
    src/txn/lock_manager.cpp
    src/txn/transaction.cpp
    src/txn/deadlock_detector.cpp
)
target_link_libraries(minidb_txn minidb_storage Threads::Threads)

# ─── Recovery module ──────────────────────────────────────────
add_library(minidb_recovery
    src/recovery/wal.cpp
    src/recovery/recovery_manager.cpp
)
target_link_libraries(minidb_recovery minidb_storage minidb_txn)

# ─── LSM storage extension ────────────────────────────────────
add_library(minidb_lsm
    src/storage_lsm/memtable.cpp
    src/storage_lsm/sstable.cpp
    src/storage_lsm/compaction.cpp
)

# ─── REPL / Main executable ───────────────────────────────────
add_executable(minidb
    src/repl/main.cpp
)
target_link_libraries(minidb
    minidb_exec
    minidb_plan
    minidb_sql
    minidb_index
    minidb_catalog
    minidb_storage
    minidb_txn
    minidb_recovery
    minidb_lsm
    Threads::Threads
)

# ─── Testing (Catch2 via FetchContent) ────────────────────────
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(Catch2)

# Helper macro to add test executables
macro(add_minidb_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME}
        Catch2::Catch2WithMain
        minidb_exec
        minidb_plan
        minidb_sql
        minidb_index
        minidb_catalog
        minidb_storage
        minidb_txn
        minidb_recovery
        minidb_lsm
        Threads::Threads
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endmacro()

enable_testing()

add_minidb_test(test_storage     tests/test_storage.cpp)
add_minidb_test(test_catalog     tests/test_catalog.cpp)
add_minidb_test(test_index       tests/test_index.cpp)
add_minidb_test(test_sql         tests/test_sql.cpp)
add_minidb_test(test_exec        tests/test_exec.cpp)
add_minidb_test(test_optimizer   tests/test_optimizer.cpp)
add_minidb_test(test_txn         tests/test_txn.cpp)
add_minidb_test(test_recovery    tests/test_recovery.cpp)
add_minidb_test(test_lsm         tests/test_lsm.cpp)

# ─── Benchmark executable ─────────────────────────────────────
add_executable(minidb_benchmark benchmarks/benchmark_main.cpp)
target_link_libraries(minidb_benchmark
    minidb_exec
    minidb_plan
    minidb_sql
    minidb_index
    minidb_catalog
    minidb_storage
    minidb_txn
    minidb_recovery
    minidb_lsm
    Threads::Threads
)
