cmake_minimum_required(VERSION 3.10)
project(index_lab CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(rbt rbt.cpp)
add_executable(btree btree.cpp)
