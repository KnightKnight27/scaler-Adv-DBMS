cmake_minimum_required(VERSION 3.10)
project(ClockSweepCache LANGUAGES CXX)

# Require C++17 standard (essential for std::optional and other modern features)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find multi-threading support (required for background sweep and concurrent testing)
find_package(Threads REQUIRED)

# Define the executable target and list its source files
add_executable(clock_sweep_cache main.cpp ClockSweep.hpp)

# Link the standard multi-threading library (handles -pthread on Linux/macOS automatically)
target_link_libraries(clock_sweep_cache PRIVATE Threads::Threads)

# Configure compiler warning flags for safety and code quality
if(MSVC)
    target_compile_options(clock_sweep_cache PRIVATE /W4 /WX) # Treat warnings as errors on MSVC
else()
    target_compile_options(clock_sweep_cache PRIVATE -Wall -Wextra -Werror) # Treat warnings as errors on GCC/Clang
endif()
