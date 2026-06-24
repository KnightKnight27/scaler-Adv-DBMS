cmake_minimum_required(VERSION 3.10)
project(Lab8_MVCC_2PL)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(lab8_demo
    main.cpp
    db_engine.cpp
)

target_include_directories(lab8_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
