cmake_minimum_required(VERSION 3.10)
project(btree_index CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(btree_index main.cpp)
target_compile_options(btree_index PRIVATE -Wall -Wextra -Wpedantic)
