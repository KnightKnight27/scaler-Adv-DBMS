cmake_minimum_required(VERSION 3.10)
project(TreeLab)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(treelab 
    main.cpp 
    RBTree.cpp 
    BTree.cpp
)