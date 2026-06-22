cmake_minimum_required(VERSION 3.10)
project(Lab8MVCC2PLDeadlock)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mvcc_2pl_deadlock
    main.cpp
    transaction_manager.cpp
)