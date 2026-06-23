cmake_minimum_required(VERSION 3.10)

project(MVCC_2PL_Deadlock)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mvcc_2pl mvcc_2pl.cpp)