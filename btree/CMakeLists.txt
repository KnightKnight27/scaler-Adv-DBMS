cmake_minimum_required(VERSION 3.10)
project(btree_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(btree_demo main.cpp)

target_include_directories(btree_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
