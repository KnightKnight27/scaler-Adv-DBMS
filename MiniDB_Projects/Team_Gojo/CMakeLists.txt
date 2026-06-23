cmake_minimum_required(VERSION 3.14)
project(MiniDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Full MiniDB system demo ─────────────────────────────────────────────
add_executable(minidb
    src/DiskManager.cpp
    src/BufferPool.cpp
    src/BPlusTree.cpp
    src/Catalog.cpp
    src/Table.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/Optimizer.cpp
    src/LockManager.cpp
    src/LogManager.cpp
    src/RecoveryManager.cpp
    src/main.cpp
)

target_include_directories(minidb PRIVATE src)
