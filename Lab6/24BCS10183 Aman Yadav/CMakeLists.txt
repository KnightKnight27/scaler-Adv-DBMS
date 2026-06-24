cmake_minimum_required(VERSION 3.15)
project(adbms_lab6_btree CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(btree_demo main.cpp)

# Header-only library; the .hpp lives next to main.cpp.
target_include_directories(btree_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(btree_demo PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow)
endif()
