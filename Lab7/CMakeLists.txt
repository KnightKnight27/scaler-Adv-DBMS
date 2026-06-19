project(Lab7)

add_executable(Lab7
    main.cpp
    shunting_yard.cpp
    sql_parser.cpp)

target_compile_features(Lab7 PRIVATE cxx_std_17)
