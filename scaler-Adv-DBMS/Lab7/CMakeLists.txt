cmake_minimum_required(VERSION 3.12)
project(sql_lex_postfix_eval)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Werror")

add_executable(sql_eval_test main.cpp)
