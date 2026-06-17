cmake_minimum_required(VERSION 3.16)
project(DoraDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Main executable ----
add_executable(doradb
    src/main.cpp
    src/common/types.cpp
    src/storage/disk_manager.cpp
    src/storage/page.cpp
    src/storage/buffer_pool.cpp
    src/storage/heap_file.cpp
    src/index/bplus_tree.cpp
    src/catalog/catalog.cpp
    src/parser/tokenizer.cpp
    src/parser/parser.cpp
)

target_include_directories(doradb PRIVATE src)
