
cmake_minimum_required(VERSION 3.12)

project(ClockSweep)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ClockSweepTest

set(PROJECT_SOURCES
	src/main.cc
)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})
