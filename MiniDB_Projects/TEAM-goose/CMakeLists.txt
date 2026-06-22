cmake_minimum_required(VERSION 3.16)
project(MiniDB VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# Find readline (for CLI interactive mode)
find_library(READLINE_LIBRARY readline)
if(NOT READLINE_LIBRARY)
    message(WARNING "readline not found — CLI will use basic stdin input")
    add_compile_definitions(NO_READLINE)
endif()

# ── MiniDB Library ──────────────────────────────────────────────────────
set(MINIDB_SOURCES
    src/common/types.cpp
)

add_library(minidb STATIC ${MINIDB_SOURCES})
target_include_directories(minidb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(minidb PUBLIC cxx_std_17)

# ── MiniDB CLI ───────────────────────────────────────────────────────────
add_executable(minidb_cli src/main.cpp)
target_link_libraries(minidb_cli minidb)
if(READLINE_LIBRARY)
    target_link_libraries(minidb_cli ${READLINE_LIBRARY})
endif()

# ── Benchmarks ──────────────────────────────────────────────────────────
add_executable(benchmark benchmarks/benchmark.cpp)
target_link_libraries(benchmark minidb)

# ── Tests ────────────────────────────────────────────────────────────────
enable_testing()
file(GLOB TEST_SOURCES tests/*.cpp)
foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} minidb)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
