cmake_minimum_required(VERSION 3.16)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

file(GLOB_RECURSE MINIDB_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER MINIDB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
list(FILTER MINIDB_SOURCES EXCLUDE REGEX ".*/replication/(primary_main|replica_main)\\.cpp$")

add_library(minidb_core ${MINIDB_SOURCES})
target_include_directories(minidb_core PUBLIC src)
target_link_libraries(minidb_core PUBLIC Threads::Threads)

add_executable(minidb src/main.cpp)
target_link_libraries(minidb PUBLIC minidb_core)

add_executable(minidb_primary src/replication/primary_main.cpp)
target_link_libraries(minidb_primary PUBLIC minidb_core)

add_executable(minidb_replica src/replication/replica_main.cpp)
target_link_libraries(minidb_replica PUBLIC minidb_core)

enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)
