cmake_minimum_required(VERSION 3.10)
project(lab8_txn_manager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

add_executable(lab8
    main.cpp
    lock_manager.cpp
    txn_manager.cpp
)
