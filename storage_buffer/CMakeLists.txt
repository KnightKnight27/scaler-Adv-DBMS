cmake_minimum_required(VERSION 3.10)

project(ClockSweep)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(
    ClockSweep
    src/main.cpp
)

target_include_directories(
    ClockSweep
    PRIVATE src
)   