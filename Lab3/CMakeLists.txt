cmake_minimum_required(VERSION 3.16)
project(db_engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(db_engine src/main.cpp)
target_link_libraries(db_engine pthread)
