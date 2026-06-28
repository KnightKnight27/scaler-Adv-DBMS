cmake_minimum_required(VERSION 3.20)
project(minidb_track_b LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(minidb
  src/minidb/common/types.cpp
  src/minidb/storage/page.cpp
  src/minidb/storage/disk_manager.cpp
  src/minidb/storage/page_manager.cpp
  src/minidb/storage/heap_table.cpp
  src/minidb/buffer/buffer_pool.cpp
  src/minidb/index/b_plus_tree.cpp
  src/minidb/sql/parser.cpp
  src/minidb/execution/execution_engine.cpp
  src/minidb/transaction/transaction_manager.cpp
  src/minidb/recovery/wal.cpp
)

target_include_directories(minidb PUBLIC src)

enable_testing()
add_executable(minidb_tests src/tests/minidb_tests.cpp)
target_link_libraries(minidb_tests PRIVATE minidb)
add_test(NAME minidb_tests COMMAND minidb_tests)
