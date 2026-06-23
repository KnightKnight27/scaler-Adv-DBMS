cmake_minimum_required(VERSION 3.16)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# Core library: every src/*.cpp except entry points (the CLI and the demos).
file(GLOB_RECURSE MINIDB_LIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER MINIDB_LIB_SOURCES EXCLUDE REGEX "/src/main\\.cpp$")
list(FILTER MINIDB_LIB_SOURCES EXCLUDE REGEX "/src/demo/")

add_library(minidb ${MINIDB_LIB_SOURCES})
target_include_directories(minidb PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(minidb PUBLIC Threads::Threads)

# Entry points are added only once their source exists (incremental build).
if(EXISTS ${CMAKE_SOURCE_DIR}/src/main.cpp)
  add_executable(minidb_cli src/main.cpp)
  target_link_libraries(minidb_cli PRIVATE minidb)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/demo/txn_demo.cpp)
  add_executable(txn_demo src/demo/txn_demo.cpp)
  target_link_libraries(txn_demo PRIVATE minidb)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/src/demo/recovery_demo.cpp)
  add_executable(recovery_demo src/demo/recovery_demo.cpp)
  target_link_libraries(recovery_demo PRIVATE minidb)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/benchmarks/bench_storage.cpp)
  add_executable(minidb_bench benchmarks/bench_storage.cpp)
  target_link_libraries(minidb_bench PRIVATE minidb)
endif()

# One test executable per tests/*.cpp.
enable_testing()
file(GLOB MINIDB_TESTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
foreach(test_src ${MINIDB_TESTS})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE minidb)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
