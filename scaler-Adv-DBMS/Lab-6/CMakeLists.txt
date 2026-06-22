cmake_minimum_required(VERSION 3.12)
project(mvcc_concurrency_lab)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Werror -pthread")

add_executable(concurrency_tester transaction_manager.cpp)
target_link_libraries(concurrency_tester pthread)
