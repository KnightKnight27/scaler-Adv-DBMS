cmake_minimum_required(VERSION 3.15)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
 set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wno-unqualified-std-cast-call)

include_directories(${PROJECT_SOURCE_DIR}/src)

set(MINIDB_SOURCES src/main.cpp src/common/types.cpp src/common/rid.cpp src/common/tuple.cpp
 src/storage/page.cpp
 src/storage/disk_manager.cpp
 src/storage/heap_file.cpp
 src/storage/columnar.cpp
 src/catalog/table_heap.cpp
 src/catalog/catalog.cpp
 src/index/b_plus_tree.cpp
 src/execution/executor.cpp
 src/execution/vectorized.cpp
 src/parser/token.cpp
 src/parser/tokenizer.cpp
 src/parser/parser.cpp
 src/executor/evaluator.cpp
 src/planner/planner.cpp
 src/optimizer/stats.cpp
 src/optimizer/optimizer.cpp
 src/buffer/buffer_pool.cpp
 src/transaction/lock_manager.cpp
 src/recovery/log_record.cpp
 src/recovery/log_manager.cpp
 src/recovery/recovery.cpp
 src/transaction/transaction.h)

add_executable(minidb ${MINIDB_SOURCES})

add_executable(benchmark_vectorized
  benchmarks/benchmark.cpp
  src/common/types.cpp
  src/common/rid.cpp
  src/common/tuple.cpp
  src/storage/page.cpp
  src/storage/disk_manager.cpp
  src/storage/heap_file.cpp
  src/storage/columnar.cpp
  src/catalog/table_heap.cpp
  src/catalog/catalog.cpp
  src/index/b_plus_tree.cpp
  src/execution/executor.cpp
  src/execution/vectorized.cpp
  src/buffer/buffer_pool.cpp
)

enable_testing()
add_subdirectory(tests)
