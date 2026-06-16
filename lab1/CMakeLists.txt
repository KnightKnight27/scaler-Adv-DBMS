cmake_minimum_required(VERSION 3.20)
project(kernel_io_internals)

set(CMAKE_CXX_STANDARD 20)

add_executable(kernel_io_internals main.cpp
        main.cpp
        kprint.cpp
        kprint.h
        syscalls.h
        syscalls.cpp
)
