cmake_minimum_required(VERSION 3.10)
project(Advanced_DB_Sim)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(db_simulator
    main.cpp
    db_engine.cpp
)

target_include_directories(db_simulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
