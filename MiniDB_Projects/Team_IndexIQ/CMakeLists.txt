cmake_minimum_required(VERSION 3.16)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src)

set(COMMON_SRCS
    src/storage/heap_file.cpp
    src/storage/buffer_pool.cpp
    src/index/btree.cpp
    src/catalog/catalog.cpp
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/recovery/wal.cpp
    src/transaction/txn_manager.cpp
    src/optimizer/optimizer.cpp
    src/executor/executor.cpp
    src/db.cpp
)

add_executable(minidb ${COMMON_SRCS} src/main.cpp)
target_link_libraries(minidb pthread)

add_executable(minidb_demo ${COMMON_SRCS} demo/demo.cpp)
target_link_libraries(minidb_demo pthread)

add_executable(minidb_bench
    src/transaction/txn_manager.cpp
    benchmarks/bench.cpp
)
target_link_libraries(minidb_bench pthread)
