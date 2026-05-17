cmake_minimum_required(VERSION 3.10)
project(db_engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(db_engine main.cpp)
target_link_libraries(db_engine Threads::Threads)
