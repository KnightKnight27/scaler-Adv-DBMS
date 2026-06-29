cmake_minimum_required(VERSION 3.16)
project(Lab7 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(Lab7
    src/main.cpp
    src/expr/Token.cpp
    src/expr/OperatorTable.cpp
    src/expr/Tokenizer.cpp
    src/expr/ShuntingYard.cpp
    src/expr/RpnEvaluator.cpp
    src/expr/ExpressionEvaluator.cpp
    src/sql/Value.cpp
    src/sql/Ast.cpp
    src/sql/SqlLexer.cpp
    src/sql/SqlParser.cpp
    src/sql/QueryEngine.cpp
)

target_include_directories(Lab7 PRIVATE include)
