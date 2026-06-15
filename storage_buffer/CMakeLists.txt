cmake_minimum_required(VERSION 3.10)

project(ClockSweepCache)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(
    ClockSweepCache
    src/main.cpp
)

target_include_directories(
    ClockSweepCache
    PRIVATE src
)