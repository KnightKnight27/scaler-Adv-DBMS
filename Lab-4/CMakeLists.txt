cmake_minimum_required(VERSION 3.10)
project(lab4_data_structures CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(b_tree "b-tree/main.cpp")
target_compile_options(b_tree PRIVATE -Wall -Wextra -Wpedantic)

add_executable(hex_dump "hex dump/main.cpp")
target_compile_options(hex_dump PRIVATE -Wall -Wextra -Wpedantic)

add_executable(red_black_tree "red black tree/main.cpp")
target_compile_options(red_black_tree PRIVATE -Wall -Wextra -Wpedantic)