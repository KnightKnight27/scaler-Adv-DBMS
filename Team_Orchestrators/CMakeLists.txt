cmake_minimum_required(VERSION 3.20)
project(MiniDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Core library: everything except the entry point ----
file(GLOB_RECURSE MINIDB_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(minidb_core ${MINIDB_SOURCES})
target_include_directories(minidb_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

# ---- REPL executable ----
add_executable(minidb apps/repl/main.cpp)
target_link_libraries(minidb PRIVATE minidb_core)

# ---- Benchmark harness ----
add_executable(minidb_bench apps/bench/main.cpp)
target_link_libraries(minidb_bench PRIVATE minidb_core)

# ---- Tests ----
enable_testing()
file(GLOB MINIDB_TESTS CONFIGURE_DEPENDS tests/test_*.cpp)
foreach(test_src ${MINIDB_TESTS})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE minidb_core)
  target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
