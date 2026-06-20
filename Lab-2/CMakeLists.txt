cmake_minimum_required(VERSION 3.10)
project(file_io_lab)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

add_executable(reader reader.cpp)

# Instructions for strace:
# strace -e trace=openat,read,close,fstat,mmap ./reader
