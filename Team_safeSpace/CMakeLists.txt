cmake_minimum_required(VERSION 3.16)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# Threads (lock manager, concurrency demos).
find_package(Threads REQUIRED)

# ---- Core engine library: every src/*.cpp except the CLI entry point --------
file(GLOB_RECURSE MINIDB_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(FILTER MINIDB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

add_library(minidb_lib STATIC ${MINIDB_SOURCES})
target_include_directories(minidb_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(minidb_lib PUBLIC Threads::Threads)

# ---- CLI / REPL (built only once src/main.cpp exists) -----------------------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
  add_executable(minidb src/main.cpp)
  target_link_libraries(minidb PRIVATE minidb_lib)
endif()

# ---- Tests: one executable per tests/test_*.cpp, registered with CTest ------
enable_testing()
file(GLOB MINIDB_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
foreach(test_src ${MINIDB_TESTS})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE minidb_lib)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# ---- Benchmarks: one executable per benchmarks/bench_*.cpp ------------------
file(GLOB MINIDB_BENCHES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/bench_*.cpp)
foreach(bench_src ${MINIDB_BENCHES})
  get_filename_component(bench_name ${bench_src} NAME_WE)
  add_executable(${bench_name} ${bench_src})
  target_link_libraries(${bench_name} PRIVATE minidb_lib)
endforeach()
