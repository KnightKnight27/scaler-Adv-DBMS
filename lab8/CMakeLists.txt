cmake_minimum_required(VERSION 3.10)

project(TransactionControlDemo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(transaction_control_demo
    main.cpp
    transaction_manager.cpp
)
cmake