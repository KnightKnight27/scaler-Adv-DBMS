cmake_minimum_required(VERSION 3.16)
project(DoraDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(doradb
    src/main.cpp
    src/common/types.cpp
    src/storage/disk_manager.cpp
    src/storage/page.cpp
    src/storage/buffer_pool.cpp
    src/storage/heap_file.cpp
    src/storage/heap_engine.cpp
    src/index/bplus_tree.cpp
    src/catalog/catalog.cpp
    src/parser/tokenizer.cpp
    src/parser/parser.cpp
    src/executor/executor.cpp
    src/optimizer/optimizer.cpp
    src/txn/lock_manager.cpp
    src/txn/txn_manager.cpp
    src/recovery/wal.cpp
    src/recovery/recovery.cpp
    src/lsm/memtable.cpp
    src/lsm/sstable.cpp
    src/lsm/lsm_engine.cpp
)

target_include_directories(doradb PRIVATE src)
