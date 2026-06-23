cmake_minimum_required(VERSION 3.15)
project(MVCC_Engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(txmgr
    main.cpp
    common.h
    tx_registry.h
    tx_registry.cpp
    mvcc_heap.h
    mvcc_heap.cpp
    lock_manager.h
    lock_manager.cpp
    tx_manager.h
    tx_manager.cpp
)

target_link_libraries(txmgr PRIVATE Threads::Threads)
