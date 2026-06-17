cmake_minimum_required(VERSION 3.15)
project(advanced_indexing CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define compile executable including both Red-Black Tree and B-Tree implementations
add_executable(indexing_demo main.cpp rbt.cpp btree.cpp)