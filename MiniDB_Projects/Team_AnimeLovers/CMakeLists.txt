cmake_minimum_required(VERSION 3.14)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Warnings on, optimise for debugging clarity over speed (matches course goals)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g")

# Core database library (all modules link against this)
add_library(minidb_lib
    src/storage.cpp
    src/bplustree.cpp
    src/parser.cpp
    src/engine.cpp
    src/transaction.cpp
)
target_include_directories(minidb_lib PUBLIC src)

# Main REPL executable
add_executable(minidb src/main.cpp)
target_link_libraries(minidb PRIVATE minidb_lib)

# 2PL vs MVCC benchmark executable
add_executable(bench benchmarks/bench_2pl_vs_mvcc.cpp)
target_link_libraries(bench PRIVATE minidb_lib)

# 2PL deadlock-detection demonstration
add_executable(deadlock_demo benchmarks/deadlock_demo.cpp)
target_link_libraries(deadlock_demo PRIVATE minidb_lib)
find_package(Threads REQUIRED)
target_link_libraries(deadlock_demo PRIVATE Threads::Threads)
