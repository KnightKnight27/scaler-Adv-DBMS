cmake_minimum_required(VERSION 3.14)

# All source files that make up the database library.
add_library(minidb_lib
    storage/page_manager.cpp
    storage/buffer_pool.cpp
    storage/heap_file.cpp
    index/bplus_tree.cpp
    catalog/catalog.cpp
    query/lexer.cpp
    query/parser.cpp
    query/executor.cpp
    optimizer/optimizer.cpp
    txn/transaction_manager.cpp
    wal/wal.cpp
)

# So that #include "storage/page.h" works from any file in src/
target_include_directories(minidb_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The main demo executable.
add_executable(minidb main.cpp)
target_link_libraries(minidb minidb_lib)
