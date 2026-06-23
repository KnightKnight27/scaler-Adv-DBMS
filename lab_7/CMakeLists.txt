cmake_minimum_required(VERSION 3.10)
project(lab7_shunting_yard CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

add_executable(lab7
    main.cpp
    lexer.cpp
    shunting_yard.cpp
    sql.cpp
)
