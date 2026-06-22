cmake_minimum_required(VERSION 3.16)
project(lab8_transaction_manager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(lab8
    src/main.cpp
    src/txn/Types.cpp
    src/txn/VersionStore.cpp
    src/txn/LockManager.cpp
    src/txn/DeadlockDetector.cpp
    src/txn/TransactionManager.cpp
)

target_include_directories(lab8 PRIVATE include)
