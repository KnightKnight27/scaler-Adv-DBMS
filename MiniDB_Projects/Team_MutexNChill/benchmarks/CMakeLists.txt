cmake_minimum_required(VERSION 3.14)

add_executable(minidb_bench benchmark.cpp)

# Link pthreads (needed for std::thread on Linux/macOS)
find_package(Threads REQUIRED)
target_link_libraries(minidb_bench Threads::Threads)
