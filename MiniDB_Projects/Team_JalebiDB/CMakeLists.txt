cmake_minimum_required(VERSION 3.12)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Thread package for parallel transactions and socket worker threads
find_package(Threads REQUIRED)

# Include directories
include_directories(src)

# Source files
set(SOURCES
    src/storage/disk_manager.cpp
    src/storage/buffer_pool_manager.cpp
    src/index/b_plus_tree.cpp
    src/recovery/log_manager.cpp
    src/recovery/recovery_manager.cpp
    src/concurrency/lock_manager.cpp
    src/concurrency/transaction_manager.cpp
    src/parser/parser.cpp
    src/optimizer/optimizer.cpp
    src/distributed/replica_manager.cpp
)

# Build main interactive CLI executable
add_executable(minidb src/main.cpp ${SOURCES})
target_link_libraries(minidb PRIVATE Threads::Threads)

# Build performance benchmarking suite executable
add_executable(minidb_benchmark benchmarks/benchmark.cpp ${SOURCES})
target_link_libraries(minidb_benchmark PRIVATE Threads::Threads)

add_executable(test_run src/test_run.cpp ${SOURCES})
target_link_libraries(test_run PRIVATE Threads::Threads)
