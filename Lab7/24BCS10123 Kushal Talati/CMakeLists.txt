cmake_minimum_required(VERSION 3.14)
project(mini_sql_lab CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mini_sql_lab runner.cpp)
target_include_directories(mini_sql_lab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(mini_sql_lab PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -O2)
endif()
