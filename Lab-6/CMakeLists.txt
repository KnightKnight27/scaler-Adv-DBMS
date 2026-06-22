cmake_minimum_required(VERSION 3.10)
project(transaction_manager_lab)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra -pthread")

add_executable(txn_manager transaction_manager.cpp)
target_link_libraries(txn_manager pthread)
