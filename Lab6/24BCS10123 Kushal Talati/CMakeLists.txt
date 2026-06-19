cmake_minimum_required(VERSION 3.14)
project(btree_lab CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(btree_lab runner.cpp)
target_include_directories(btree_lab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(btree_lab PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -O2)
endif()
