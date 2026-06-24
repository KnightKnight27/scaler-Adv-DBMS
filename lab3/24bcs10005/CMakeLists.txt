cmake_minimum_required(VERSION 3.10)

project(clock_sweep_cache CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(clock_sweep_cache main.cpp)

target_link_libraries(clock_sweep_cache PRIVATE Threads::Threads)