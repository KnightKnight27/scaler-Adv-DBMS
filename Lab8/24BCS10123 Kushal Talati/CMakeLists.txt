cmake_minimum_required(VERSION 3.14)
project(txn_store_lab CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(txn_store_lab runner.cpp)
target_include_directories(txn_store_lab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(txn_store_lab PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -O2)
endif()
