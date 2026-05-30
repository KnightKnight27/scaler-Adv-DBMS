cmake_minimum_required(VERSION 4.2)
project(scaler_Adv_DBMS)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(scaler_Adv_DBMS
        DefaultComparator.h
        IComparator.h
        ITree.h
        main.cc
        RBNode.h
        RedBlackTree.h)
