cmake_minimum_required(VERSION 3.12)

project(TxnEngineDemo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(
    txn_engine_demo
    main.cpp
    transaction_manager.cpp
)