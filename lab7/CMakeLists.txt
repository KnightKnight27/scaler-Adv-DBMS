cmake_minimum_required(VERSION 3.10)
project(lab7_query_frontend CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(lab7_shunting_yard shunting_yard.cpp)
add_executable(lab7_query_parser query_parser.cpp)
