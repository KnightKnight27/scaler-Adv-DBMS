cmake_minimum_required(VERSION 3.10)
project(sql_query_parser)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra")

add_executable(query_engine main.cpp)
