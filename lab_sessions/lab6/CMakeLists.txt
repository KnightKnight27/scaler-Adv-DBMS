project(MVCC_Engine)

find_package(Threads REQUIRED)

add_executable(txmgr
    main.cpp
    common.h
    mvcc_heap.h
    mvcc_heap.cpp
    lock_manager.h
    lock_manager.cpp
    tx_manager.h
    tx_manager.cpp
)

target_link_libraries(txmgr PRIVATE Threads::Threads)