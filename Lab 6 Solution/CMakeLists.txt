cmake_minimum_required(VERSION 3.10)
project(Lab Session 6: Transaction Manager — MVCC + Two-Phase Locking in C++)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(txnmanager 
    main.cpp 
    TransactionManager.cpp
)