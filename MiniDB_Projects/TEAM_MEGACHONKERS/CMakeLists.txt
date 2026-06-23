cmake_minimum_required(VERSION 3.14)
project(MiniDB VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/v1.14.0.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_library(minidb_core STATIC
    src/storage/lsm/memtable.cpp
    src/storage/lsm/sstable_writer.cpp
    src/storage/lsm/sstable_reader.cpp
    src/storage/lsm/compactor.cpp
    src/recovery/wal.cpp
    src/catalog/catalog.cpp
    src/concurrency/lock_manager.cpp
    src/storage/btree/bplus_tree.cpp

    src/recovery/recovery_manager.cpp
    src/execution/delete_executor.cpp

    src/execution/insert_executor.cpp
    src/execution/seq_scan_executor.cpp
    src/execution/join_executor.cpp
)

target_include_directories(minidb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(minidb_core PUBLIC Threads::Threads)

add_executable(minidb src/main.cpp)
target_link_libraries(minidb PRIVATE minidb_core)

enable_testing()

add_executable(lsm_test tests/lsm_test.cpp)
target_link_libraries(lsm_test PRIVATE minidb_core gtest_main)

add_executable(wal_test tests/wal_test.cpp)
target_link_libraries(wal_test PRIVATE minidb_core gtest_main)

add_executable(execution_test tests/execution_test.cpp)
target_link_libraries(execution_test PRIVATE minidb_core gtest_main)

add_executable(recovery_test tests/recovery_test.cpp)
target_link_libraries(recovery_test PRIVATE minidb_core gtest_main)

add_executable(filter_delete_test tests/filter_delete_test.cpp)
target_link_libraries(filter_delete_test PRIVATE minidb_core gtest_main)

add_executable(transaction_test tests/transaction_test.cpp)
target_link_libraries(transaction_test PRIVATE minidb_core gtest_main)

add_executable(index_test tests/index_test.cpp)
target_link_libraries(index_test PRIVATE minidb_core gtest_main)

add_executable(optimizer_test tests/optimizer_test.cpp)
target_link_libraries(optimizer_test PRIVATE minidb_core gtest_main)

include(GoogleTest)
gtest_discover_tests(lsm_test)
gtest_discover_tests(wal_test)
gtest_discover_tests(execution_test)
gtest_discover_tests(recovery_test)
gtest_discover_tests(filter_delete_test)
gtest_discover_tests(transaction_test)
gtest_discover_tests(index_test)
gtest_discover_tests(optimizer_test)



# Benchmarks
add_executable(lsm_benchmark benchmarks/lsm_benchmark.cpp)
target_link_libraries(lsm_benchmark PRIVATE minidb_core)