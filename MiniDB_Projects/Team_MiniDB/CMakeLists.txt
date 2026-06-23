cmake_minimum_required(VERSION 3.14)
project(MiniDB VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Member 1: storage only — Members 2 & 3 add sources in their commits
add_executable(minidb
    src/main.cpp
    src/storage/Page.cpp
    src/storage/PageManager.cpp
    src/storage/HeapFile.cpp
    src/storage/BufferPool.cpp
)

target_include_directories(minidb PRIVATE src)
