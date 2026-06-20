cmake_minimum_required(VERSION 3.10)
project(clock_sweep_lab)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra")

add_executable(clock_sweep clock_sweep.cpp)
