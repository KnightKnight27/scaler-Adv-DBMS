cmake_minimum_required(VERSION 3.12)
project(Team_JustChill CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files for Track 2 (The Storage Mechanic)
set(SOURCES
    src/page.cpp
    src/heap_file.cpp
    src/buffer_pool.cpp
)

# Header files
set(HEADERS
    src/page.h
    src/heap_file.h
    src/buffer_pool.h
)

# Build a static library for storage
add_library(storage STATIC ${SOURCES} ${HEADERS})
target_include_directories(storage PUBLIC src)

# Build the benchmark/test executable
add_executable(benchmark benchmarks/benchmark.cpp)
target_link_libraries(benchmark PRIVATE storage)
