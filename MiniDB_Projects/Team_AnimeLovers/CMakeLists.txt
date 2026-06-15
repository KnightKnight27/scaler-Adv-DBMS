cmake_minimum_required(VERSION 3.16)
project(MiniDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warn aggressively; the code should be clean.
add_compile_options(-Wall -Wextra -Wpedantic)

# --- Source files shared by all targets ---
set(MINIDB_SRC
    src/storage.cpp
    src/bplustree.cpp
    src/parser.cpp
    src/transaction.cpp
    src/engine.cpp
)

# --- Interactive shell ---
add_executable(minidb src/main.cpp ${MINIDB_SRC})

# --- Test suite ---
add_executable(minidb_tests tests/test.cpp ${MINIDB_SRC})

# --- Benchmark (2PL vs MVCC) ---
add_executable(benchmark benchmarks/benchmark.cpp ${MINIDB_SRC})

# All three targets need the source directory on the include path.
foreach(t minidb minidb_tests benchmark)
    target_include_directories(${t} PRIVATE src)
    # Link pthreads — used by the benchmark and transaction layer.
    find_package(Threads REQUIRED)
    target_link_libraries(${t} PRIVATE Threads::Threads)
endforeach()
