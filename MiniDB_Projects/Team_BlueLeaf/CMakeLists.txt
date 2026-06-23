cmake_minimum_required(VERSION 3.16)
project(minidb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# All engine sources except the CLI entry point go into a static core library
# that the executable, tests, and benchmarks all link against.
file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM CORE_SRC ${CMAKE_SOURCE_DIR}/src/main.cpp)

add_library(minidb_core STATIC ${CORE_SRC})
target_include_directories(minidb_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

find_package(Threads REQUIRED)
target_link_libraries(minidb_core PUBLIC Threads::Threads)

add_executable(minidb ${CMAKE_SOURCE_DIR}/src/main.cpp)
target_link_libraries(minidb PRIVATE minidb_core)
