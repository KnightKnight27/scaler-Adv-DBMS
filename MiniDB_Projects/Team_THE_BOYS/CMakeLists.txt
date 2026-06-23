cmake_minimum_required(VERSION 3.16)
project(MiniDB VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

set(MINIDB_SOURCES
    src/common/types.cpp
    src/storage/page.cpp
    src/storage/page_manager.cpp
    src/storage/buffer_pool.cpp
    src/storage/heap_file.cpp
    src/index/btree.cpp
    src/catalog/catalog.cpp
    src/parser/sql_parser.cpp
    src/optimizer/optimizer.cpp
    src/executor/executor.cpp
    src/executor/vectorized.cpp
    src/executor/execution_metrics.cpp
    src/transaction/lock_manager.cpp
    src/transaction/transaction_manager.cpp
    src/recovery/wal.cpp
    src/recovery/recovery_manager.cpp
    src/engine/database.cpp
)

add_library(minidb_lib ${MINIDB_SOURCES})
target_include_directories(minidb_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(minidb src/main.cpp)
target_link_libraries(minidb PRIVATE minidb_lib)

add_executable(benchmark benchmarks/benchmark.cpp)
target_link_libraries(benchmark PRIVATE minidb_lib)

add_executable(core_demos tests/core_demos.cpp src/transaction/lock_manager.cpp src/common/types.cpp)
target_include_directories(core_demos PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(core_demos PRIVATE minidb_lib)
