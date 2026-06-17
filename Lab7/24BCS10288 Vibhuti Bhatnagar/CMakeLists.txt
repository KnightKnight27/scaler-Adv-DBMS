cmake_minimum_required(VERSION 3.10)
project(sql_engine_demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(sql_engine_demo main.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(sql_engine_demo PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow)
endif()
