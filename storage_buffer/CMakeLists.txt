cmake_minimum_required(VERSION 3.10)
project(clock_buffer_pool CXX)

# Tell the compiler to use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the system's native threading library (pthread)
find_package(Threads REQUIRED)

# Build an executable named "clock_pool" from your main.cpp file
add_executable(clock_pool main.cpp)

# Attach the threading library to the executable
target_link_libraries(clock_pool PRIVATE Threads::Threads)