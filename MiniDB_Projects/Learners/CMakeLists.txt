cmake_minimum_required(VERSION 3.15)
project(MiniDB VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    src/storage/page.cpp
    src/storage/page_manager.cpp
    src/storage/buffer_pool.cpp
    src/index/bplus_tree.cpp
    src/query/parser.cpp
    src/query/optimizer.cpp
    src/query/executor.cpp
    src/tx/lock_manager.cpp
    src/tx/transaction.cpp
    src/recovery/wal.cpp
    src/recovery/recovery_mgr.cpp
    src/database.cpp
    src/distributed/node.cpp
    src/distributed/replication.cpp
)

# Header search paths
include_directories(src)

# Core library
add_library(minidb_core STATIC ${SOURCES})

# Thread library requirement for concurrent transactions / benchmarks
find_package(Threads REQUIRED)

# Executables
add_executable(minidb_main main.cpp)
target_link_libraries(minidb_main PRIVATE minidb_core Threads::Threads)

add_executable(minidb_benchmark benchmarks/benchmark.cpp)
target_link_libraries(minidb_benchmark PRIVATE minidb_core Threads::Threads)

add_executable(minidb_demo interview_demo.cpp)
target_link_libraries(minidb_demo PRIVATE minidb_core Threads::Threads)
