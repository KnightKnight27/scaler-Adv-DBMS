cmake_minimum_required(VERSION 3.10)

project(Lab5BalancedTrees LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(
    lab5
    main.cpp
    btree.cpp
    rbt.cpp
)