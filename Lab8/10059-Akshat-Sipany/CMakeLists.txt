cmake_minimum_required(VERSION 3.10)

project(TransactionManagerDemo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(
    transaction_demo
    main.cpp
    transaction_manager.cpp
)