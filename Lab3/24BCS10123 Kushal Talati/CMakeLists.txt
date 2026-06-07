cmake_minimum_required(VERSION 3.14)
project(buffer_pool_lab CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(buffer_pool_lab main.cpp)
target_include_directories(buffer_pool_lab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)
target_link_libraries(buffer_pool_lab PRIVATE Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(buffer_pool_lab PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow)
endif()
