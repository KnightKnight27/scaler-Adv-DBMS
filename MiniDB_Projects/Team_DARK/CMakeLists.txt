cmake_minimum_required(VERSION 3.14)
project(miniDB VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINIDB_ASAN "Build with AddressSanitizer" OFF)
option(MINIDB_TSAN "Build with ThreadSanitizer" OFF)

if(MINIDB_ASAN AND MINIDB_TSAN)
    message(FATAL_ERROR "MINIDB_ASAN and MINIDB_TSAN are mutually exclusive")
endif()

if(MINIDB_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(MINIDB_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
)
FetchContent_MakeAvailable(doctest)

add_library(minidb_storage
    src/storage/disk_manager.cpp
    src/storage/buffer_pool_manager.cpp
    src/storage/table_heap.cpp
)

target_include_directories(minidb_storage PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_library(minidb_index
    src/index/btree.cpp
)

target_include_directories(minidb_index PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(minidb_index PUBLIC minidb_storage)

add_library(minidb_recovery
    src/recovery/log_manager.cpp
    src/recovery/recovery_manager.cpp
)

target_include_directories(minidb_recovery PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(minidb_recovery PUBLIC minidb_storage)

add_library(minidb_concurrency
    src/concurrency/lock_manager.cpp
    src/concurrency/transaction_manager.cpp
)

target_include_directories(minidb_concurrency PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(minidb_concurrency PUBLIC minidb_storage minidb_recovery)

add_library(minidb_parser
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/parser/ast_clone.cpp
)

target_include_directories(minidb_parser PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_library(minidb_execution
    src/execution/catalog.cpp
    src/execution/optimizer.cpp
    src/execution/executor.cpp
)

target_include_directories(minidb_execution PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(minidb_execution PUBLIC minidb_parser minidb_index minidb_concurrency)

add_executable(storage_test tests/storage_test.cpp)
target_link_libraries(storage_test PRIVATE minidb_storage doctest::doctest)

add_executable(index_test tests/index_test.cpp)
target_link_libraries(index_test PRIVATE minidb_index doctest::doctest)

add_executable(concurrency_test tests/concurrency_test.cpp)
target_link_libraries(concurrency_test PRIVATE minidb_concurrency doctest::doctest)

add_executable(recovery_test tests/recovery_test.cpp)
target_link_libraries(recovery_test PRIVATE minidb_concurrency doctest::doctest)

add_executable(query_test tests/query_test.cpp)
target_link_libraries(query_test PRIVATE minidb_execution doctest::doctest)

add_executable(minidb_cli src/main.cpp)
target_link_libraries(minidb_cli PRIVATE minidb_execution)

add_executable(benchmark benchmarks/benchmark.cpp)
target_link_libraries(benchmark PRIVATE minidb_execution)

include(CTest)
add_test(NAME storage_test COMMAND storage_test)
add_test(NAME index_test COMMAND index_test)
add_test(NAME concurrency_test COMMAND concurrency_test)
add_test(NAME recovery_test COMMAND recovery_test)
add_test(NAME query_test COMMAND query_test)
