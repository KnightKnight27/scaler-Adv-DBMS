cmake_minimum_required(VERSION 3.10)

add_executable(storage_buffer_main main.cpp)
target_compile_features(storage_buffer_main PRIVATE cxx_std_17)
project(db_engine)

add_executable(db_engine main.cpp)