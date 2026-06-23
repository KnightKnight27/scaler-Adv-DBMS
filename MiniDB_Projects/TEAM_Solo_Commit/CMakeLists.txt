cmake_minimum_required(VERSION 3.15)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

# All engine sources except the entry point and benchmark/test mains.
file(GLOB_RECURSE MINIDB_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(FILTER MINIDB_SOURCES EXCLUDE REGEX "src/main\\.cpp$")

add_library(minidb_core STATIC ${MINIDB_SOURCES})
target_include_directories(minidb_core PUBLIC src)
target_link_libraries(minidb_core PUBLIC Threads::Threads)

# Interactive shell / demo driver.
add_executable(minidb src/main.cpp)
target_link_libraries(minidb PRIVATE minidb_core)

# Benchmarks (Track B MVCC vs 2PL, and index vs scan).
add_executable(bench_mvcc_vs_2pl benchmarks/bench_mvcc_vs_2pl.cpp)
target_link_libraries(bench_mvcc_vs_2pl PRIVATE minidb_core)

add_executable(bench_index_vs_scan benchmarks/bench_index_vs_scan.cpp)
target_link_libraries(bench_index_vs_scan PRIVATE minidb_core)

# Tests (one self-checking binary per subsystem, registered with CTest).
enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "tests/test_*.cpp")
foreach(test_src ${TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE minidb_core)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
