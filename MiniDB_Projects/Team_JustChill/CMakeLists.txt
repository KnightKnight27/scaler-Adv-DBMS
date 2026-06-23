cmake_minimum_required(VERSION 3.12)
project(Team_JustChill CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files for Track 2 (The Storage Mechanic) and Track 1 (WAL & Replication)
set(SOURCES
    src/page.cpp
    src/heap_file.cpp
    src/buffer_pool.cpp
    src/wal.cpp
    src/replication.cpp
)

# Header files
set(HEADERS
    src/page.h
    src/heap_file.h
    src/buffer_pool.h
    src/wal.h
    src/replication.h
)

# Build a static library for storage
add_library(storage STATIC ${SOURCES} ${HEADERS})
target_include_directories(storage PUBLIC src)

# Build the benchmark/test executable
add_executable(benchmark benchmarks/benchmark.cpp)
target_link_libraries(benchmark PRIVATE storage query_engine Threads::Threads)

# --- Track 3 (The Executor: Query & Concurrency) ---
find_package(Threads REQUIRED)

# Source files for Track 3
set(QUERY_SOURCES
    src/btree.cpp
    src/execution.cpp
    src/lock_manager.cpp
)
set(QUERY_HEADERS
    src/btree.h
    src/execution.h
    src/lock_manager.h
)

# Build a static library for the query engine
add_library(query_engine STATIC ${QUERY_SOURCES} ${QUERY_HEADERS})
target_include_directories(query_engine PUBLIC src)
target_link_libraries(query_engine PUBLIC Threads::Threads)

# Track 3 unit tests
enable_testing()
add_executable(track3_test tests/track3_test.cpp)
target_link_libraries(track3_test PRIVATE query_engine)
# On MinGW, link the runtime statically so the test exe runs without needing
# libstdc++/libwinpthread DLLs on PATH (keeps `ctest` self-contained).
if(MINGW)
    target_link_options(track3_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
add_test(NAME track3_test COMMAND track3_test)

# Track 1 & 2 unit tests
add_executable(track1_2_test tests/track1_2_test.cpp)
target_link_libraries(track1_2_test PRIVATE storage query_engine Threads::Threads)
if(MINGW)
    target_link_options(track1_2_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
add_test(NAME track1_2_test COMMAND track1_2_test)

# Loops INSERT and SELECT (point lookup) and reports throughput/latency.
add_executable(query_benchmark benchmarks/query_benchmark.cpp)
target_link_libraries(query_benchmark PRIVATE storage query_engine Threads::Threads)
if(MINGW)
    target_link_options(query_benchmark PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Main executable
add_executable(minidb src/main.cpp)
target_link_libraries(minidb PRIVATE storage query_engine Threads::Threads)
