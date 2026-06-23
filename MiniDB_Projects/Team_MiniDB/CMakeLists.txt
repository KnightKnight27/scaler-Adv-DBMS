cmake_minimum_required(VERSION 3.14)
project(MiniDB VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(minidb
    src/main.cpp src/Database.cpp
    src/storage/Page.cpp src/storage/PageManager.cpp src/storage/HeapFile.cpp src/storage/BufferPool.cpp
    src/index/BPlusTree.cpp src/catalog/Catalog.cpp
    src/parser/Lexer.cpp src/parser/Parser.cpp
    src/optimizer/Optimizer.cpp src/execution/Executor.cpp
    src/transaction/LockManager.cpp src/transaction/TransactionManager.cpp
    src/recovery/WAL.cpp src/recovery/RecoveryManager.cpp
)
target_include_directories(minidb PRIVATE src)
