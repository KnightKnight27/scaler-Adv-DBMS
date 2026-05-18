cmake_minimum_required(VERSION 3.10)
project(db_engine CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(db_engine main.cpp)

find_package(Threads REQUIRED)
target_link_libraries(db_engine PRIVATE Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(db_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
