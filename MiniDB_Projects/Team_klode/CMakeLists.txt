cmake_minimum_required(VERSION 3.16)
project(TeamKlodeMiniDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(minidb src/minidb.cpp)
target_include_directories(minidb PUBLIC src)

add_executable(minidb_cli src/main.cpp)
target_link_libraries(minidb_cli PRIVATE minidb)

add_executable(minidb_smoke tests/smoke_test.cpp)
target_link_libraries(minidb_smoke PRIVATE minidb)

enable_testing()
add_test(NAME minidb_smoke COMMAND minidb_smoke)
