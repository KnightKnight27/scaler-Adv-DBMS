cmake_minimum_required(VERSION 3.14)
project(BTreeLab CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(btree_demo main.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(btree_demo PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -O3)
endif()
