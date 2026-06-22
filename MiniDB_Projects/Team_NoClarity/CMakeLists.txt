cmake_minimum_required(VERSION 3.12)
project(minidb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find threading libraries
find_package(Threads REQUIRED)

# Include directory
include_directories(src)

# Source files
file(GLOB_RECURSE STORAGE_SRCS "src/storage/*.cpp")
file(GLOB_RECURSE OPTIMIZER_SRCS "src/optimizer/*.cpp")
file(GLOB_RECURSE RECOVERY_SRCS "src/recovery/*.cpp")
file(GLOB_RECURSE REPLICATION_SRCS "src/replication/*.cpp")

set(ALL_SRCS
    ${STORAGE_SRCS}
    ${OPTIMIZER_SRCS}
    ${RECOVERY_SRCS}
    ${REPLICATION_SRCS}
)

# Test target
add_executable(minidb_test main.cpp ${ALL_SRCS})
target_link_libraries(minidb_test PRIVATE Threads::Threads)

# Benchmark target
add_executable(minidb_benchmark benchmark.cpp ${ALL_SRCS})
target_link_libraries(minidb_benchmark PRIVATE Threads::Threads)

# Link Windows Socket Library if on Windows
if(WIN32)
    target_link_libraries(minidb_test PRIVATE ws2_32)
    target_link_libraries(minidb_benchmark PRIVATE ws2_32)
endif()
