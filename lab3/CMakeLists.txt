
cmake_minimum_required(VERSION 3.12)

project(ClockSweep)

# ClockSweepTest

set(PROJECT_SOURCES
	src/main.cc
)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})
