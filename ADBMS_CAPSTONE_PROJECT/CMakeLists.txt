cmake_minimum_required(VERSION 3.10)
project(MiniDB)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(src)

# Add source files
file(GLOB_RECURSE SRC_FILES "src/*.cpp")

# Exclude main.cpp from library
list(REMOVE_ITEM SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/server/main.cpp")

# Library target
if(SRC_FILES)
    add_library(minidb_lib ${SRC_FILES})
endif()

# Executable
add_executable(minidb src/server/main.cpp)
if(SRC_FILES)
    target_link_libraries(minidb minidb_lib)
endif()
