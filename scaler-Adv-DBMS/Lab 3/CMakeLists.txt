# ---------------------------------------------------------------------
# CMake Build Configuration for Buffer Pool Clock Replacer
# ---------------------------------------------------------------------
cmake_minimum_required(VERSION 3.12)
project(BufferPoolClockReplacer LANGUAGES CXX)

# Activate C++17 Standard (Essential for std::optional and structured mappings)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Scan system for multi-threaded programming libraries (pthread support)
find_package(Threads REQUIRED)

# Declare build target and source files
add_executable(clock_replacer main.cpp ClockSweep.hpp)

# Bind standard threading interfaces to target binary
target_link_libraries(clock_replacer PRIVATE Threads::Threads)

# Set strict warning rules across different compilers
if(MSVC)
    target_compile_options(clock_replacer PRIVATE /W4 /WX) # Warnings as errors for MSVC
else()
    target_compile_options(clock_replacer PRIVATE -Wall -Wextra -Werror) # Warnings as errors for GCC/Clang
endif()
