cmake_minimum_required(VERSION 3.15)
project(lab5_trees CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(trees main.cpp btree.cpp rbt.cpp)
