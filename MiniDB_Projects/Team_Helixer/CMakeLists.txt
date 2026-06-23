cmake_minimum_required(VERSION 3.15)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")

# All engine sources live under src/. Headers are included relative to src/.
include_directories(${CMAKE_SOURCE_DIR}/src)
file(GLOB_RECURSE MINIDB_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/src/*.cpp)

# The engine as a static library so tools/tests can link it.
list(FILTER MINIDB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
add_library(minidb_engine STATIC ${MINIDB_SOURCES})

# The interactive shell.
add_executable(minidb ${CMAKE_SOURCE_DIR}/src/main.cpp)
target_link_libraries(minidb minidb_engine)

# Threads (lock manager / background compaction).
find_package(Threads REQUIRED)
target_link_libraries(minidb_engine Threads::Threads)

# ---- Tests & benchmarks --------------------------------------------------
enable_testing()
file(GLOB TEST_SOURCES ${CMAKE_SOURCE_DIR}/tests/*.cpp)
foreach(test_src ${TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} minidb_engine)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

file(GLOB BENCH_SOURCES ${CMAKE_SOURCE_DIR}/benchmarks/*.cpp)
foreach(bench_src ${BENCH_SOURCES})
  get_filename_component(bench_name ${bench_src} NAME_WE)
  add_executable(${bench_name} ${bench_src})
  target_link_libraries(${bench_name} minidb_engine)
endforeach()
