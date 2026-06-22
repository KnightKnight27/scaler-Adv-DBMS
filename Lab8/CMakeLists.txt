cmake_minimum_required(VERSION 3.10)
project(mvcc_deadlock_scheduler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra -pthread")

add_executable(concurrency_scheduler main.cpp)
target_link_libraries(concurrency_scheduler pthread)
