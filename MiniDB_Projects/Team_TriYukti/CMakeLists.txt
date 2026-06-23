cmake_minimum_required(VERSION 3.10)
project(MiniDB)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src)

set(CORE_SOURCES
    src/storage/page.cpp
    src/storage/page_manager.cpp
    src/storage/buffer_pool.cpp
    src/index/bplus_tree.cpp
    src/transaction/transaction.cpp
    src/transaction/lock_manager.cpp
    src/recovery/log_manager.cpp
    src/recovery/recovery.cpp
    src/mvcc/mvcc_manager.cpp
    src/sql/parser.cpp
    src/sql/executor.cpp
    src/optimizer/optimizer.cpp
)

add_library(minidb_core ${CORE_SOURCES})

add_executable(minidb src/minidb.cpp)
target_link_libraries(minidb minidb_core)

add_executable(benchmark benchmarks/benchmark.cpp)
target_link_libraries(benchmark minidb_core)

add_executable(test_all tests/test_all.cpp)
target_link_libraries(test_all minidb_core)
