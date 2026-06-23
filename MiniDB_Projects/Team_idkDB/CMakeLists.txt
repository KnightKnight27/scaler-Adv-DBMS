cmake_minimum_required(VERSION 3.20)
project(minidb VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINIDB_BUILD_TESTS "Build MiniDB tests" ON)
option(MINIDB_BUILD_BENCHMARKS "Build MiniDB benchmarks" ON)

add_library(minidb
  src/storage/disk_manager.cpp
  src/storage/buffer_pool_manager.cpp
  src/storage/heap_file.cpp
  src/index/b_plus_tree.cpp
  src/query/parser.cpp
  src/query/optimizer.cpp
  src/transaction/lock_manager.cpp
  src/recovery/log_manager.cpp
  src/lsm/lsm_tree.cpp
  src/db/database.cpp
)
target_include_directories(minidb PUBLIC include)
target_compile_options(minidb PRIVATE -Wall -Wextra -Wpedantic)

add_executable(minidb_cli src/main.cpp)
target_link_libraries(minidb_cli PRIVATE minidb)

if(MINIDB_BUILD_TESTS)
  enable_testing()
  add_executable(minidb_tests tests/minidb_tests.cpp)
  target_link_libraries(minidb_tests PRIVATE minidb)
  add_test(NAME minidb_tests COMMAND minidb_tests)
endif()

if(MINIDB_BUILD_BENCHMARKS)
  add_executable(minidb_benchmark benchmarks/write_benchmark.cpp)
  target_link_libraries(minidb_benchmark PRIVATE minidb)
endif()

add_executable(minidb_transaction_demo demos/transaction_demo.cpp)
target_link_libraries(minidb_transaction_demo PRIVATE minidb)

add_executable(minidb_recovery_demo demos/recovery_demo.cpp)
target_link_libraries(minidb_recovery_demo PRIVATE minidb)

add_executable(minidb_lsm_demo demos/lsm_demo.cpp)
target_link_libraries(minidb_lsm_demo PRIVATE minidb)
