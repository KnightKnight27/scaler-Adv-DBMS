cmake_minimum_required(VERSION 3.10)
project(lab8_txn_manager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(lab8_txn_manager main.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(lab8_txn_manager PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow)
endif()
