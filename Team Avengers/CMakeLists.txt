# MiniDB — Team Avengers — Advanced DBMS Capstone
# Build:   cmake -S . -B build && cmake --build build
# Run db:  ./build/minidb
# Tests:   cd build && ctest --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(minidb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

include_directories(${CMAKE_SOURCE_DIR}/src)

# --- the database shell ----------------------------------------------------
add_executable(minidb src/main.cpp)

# --- Track B benchmark -----------------------------------------------------
add_executable(bench_mvcc benchmarks/mvcc_vs_2pl.cpp)

# --- unit tests ------------------------------------------------------------
enable_testing()
function(minidb_test name path)
  add_executable(${name} ${path})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

minidb_test(test_storage   src/storage/test_storage.cpp)
minidb_test(test_bplus     src/index/test_bplus.cpp)
minidb_test(test_parser    src/sql/test_parser.cpp)
minidb_test(test_catalog   src/catalog/test_catalog.cpp)
minidb_test(test_exec      src/execution/test_exec.cpp)
minidb_test(test_optimizer src/optimizer/test_optimizer.cpp)
minidb_test(test_txn       src/txn/test_txn.cpp)
minidb_test(test_recovery  src/recovery/test_recovery.cpp)
