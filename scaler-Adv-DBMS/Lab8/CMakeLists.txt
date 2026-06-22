cmake_minimum_required(VERSION 3.12)
project(concurrent_lock_evaluator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra -pthread")

add_executable(concurrency_evaluator main.cpp)
target_link_libraries(concurrency_evaluator pthread)
