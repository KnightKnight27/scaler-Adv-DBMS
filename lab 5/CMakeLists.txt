cmake_minimum_required(VERSION 3.15)
project(rbt_demo CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(rbt_demo main.cpp rbt.cpp)
