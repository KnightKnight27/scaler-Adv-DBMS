cmake_minimum_required(VERSION 3.14)
project(MVCCVersionChainManager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

include_directories(include)

# ── Main demo executable ─────────────────────────────────────
add_executable(mvcc_demo
    src/main.cpp
)
target_include_directories(mvcc_demo PRIVATE include)
if(NOT MSVC)
    target_link_libraries(mvcc_demo pthread)
endif()

# ── Unit-test executable ─────────────────────────────────────
add_executable(mvcc_tests
    tests/test_mvcc.cpp
)
target_include_directories(mvcc_tests PRIVATE include)
if(NOT MSVC)
    target_link_libraries(mvcc_tests pthread)
endif()

# ── CTest integration ────────────────────────────────────────
enable_testing()
add_test(NAME MVCCUnitTests COMMAND mvcc_tests)

message(STATUS "Build targets: mvcc_demo, mvcc_tests")
message(STATUS "C++ standard: C++17")
