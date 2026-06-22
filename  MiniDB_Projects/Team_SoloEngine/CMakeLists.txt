cmake_minimum_required(VERSION 3.14)
project(Team_SoloEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# Core storage + index + query + transaction + recovery library
add_library(solo_engine
    src/storage.cpp
    src/buffer_pool.cpp
    src/btree.cpp
    src/table.cpp
    src/execution.cpp
    src/optimizer.cpp
    src/transaction.cpp
    src/recovery.cpp
)
target_include_directories(solo_engine PUBLIC src)
target_compile_options(solo_engine PRIVATE -Wall -Wextra -O2)
target_link_libraries(solo_engine Threads::Threads)

# Storage layer tests
add_executable(test_storage tests/test_storage.cpp)
target_link_libraries(test_storage solo_engine)
target_compile_options(test_storage PRIVATE -Wall -Wextra -g)

# B+ Tree tests
add_executable(test_btree tests/test_btree.cpp)
target_link_libraries(test_btree solo_engine)
target_compile_options(test_btree PRIVATE -Wall -Wextra -g)

# Execution engine tests
add_executable(test_execution tests/test_execution.cpp)
target_link_libraries(test_execution solo_engine)
target_compile_options(test_execution PRIVATE -Wall -Wextra -g)

# Transaction & Recovery tests
add_executable(test_transactions tests/test_transactions.cpp)
target_link_libraries(test_transactions solo_engine)
target_compile_options(test_transactions PRIVATE -Wall -Wextra -g)

# Vectorized execution benchmark (Track A)
add_executable(bench_execution benchmarks/bench_execution.cpp)
target_link_libraries(bench_execution solo_engine)
target_compile_options(bench_execution PRIVATE -Wall -Wextra -O2)
