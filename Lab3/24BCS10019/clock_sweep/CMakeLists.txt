cmake_minimum_required(VERSION 3.10)
project(ClockSweepBuffer CXX)

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add warnings for professional code quality
add_compile_options(-Wall -Wextra)

# Define the executable
add_executable(clock_sweep main.cpp)
