cmake_minimum_required(VERSION 3.10)
project(TransactionManagerLab)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(txnmanager 
    main.cpp 
    TransactionManager.cpp
)