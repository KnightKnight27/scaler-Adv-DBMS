# Scaler Advanced Database Management Systems (Adv-DBMS) - Lab 8

This workspace contains the implementation and exercises for **Lab 8** of the Advanced DBMS curriculum. It consists of five distinct sub-projects implementing core database engine components: a B-Tree Database Index, a Clock Sweep Cache Buffer Pool Manager, an In-Memory Transaction Manager (MVCC + Strict 2PL), an Abstract Syntax Tree (AST) SQL Query Parser, and notes from past lab sessions.

---

## Workspace Structure

The project components are organized as follows:

```
.
├── index/                     # B-Tree Database Index Implementation
│   ├── CMakeLists.txt         # Build configuration
│   └── main.cpp               # C++ code for B-Tree search, insert, and split
│
├── storage_buffer/            # Cache Eviction Buffer Pool Manager
│   ├── CMakeLists.txt         # Build configuration
│   └── main.cpp               # C++ implementation of Second-Chance (Clock Sweep)
│
├── lab8/                      # In-Memory Transaction Manager
│   ├── CMakeLists.txt         # Build configuration
│   ├── README.md              # Detailed walkthrough and design patterns
│   ├── txn_manager.hpp        # Concurrency control (MVCC reads + Strict 2PL writes)
│   └── main.cpp               # Driver verifying isolation, deadlocks, and GC
│
├── query_parser/              # SQL Lexer and AST query execution engine
│   ├── app/                   # Lexer, Parser, AST Expression classes, and driver
│   │   ├── expressions.h
│   │   ├── lexer.cpp
│   │   ├── lexer.h
│   │   ├── main.cpp
│   │   ├── select_stat.h
│   │   └── types.h
│   └── lld/                   # High-level architecture pseudo-code
│       └── main.cpp
│
├── lab_sessions/              # Summary and journals of Labs 1 to 6
│   ├── lab_1.txt
│   ├── lab_2.txt
│   ├── lab_3.txt
│   ├── lab_4.txt
│   ├── lab_5.txt
│   └── lab_6.txt
│
├── reinterpret_cast.cpp       # Pointer reinterpret_cast demonstration
├── students.db                # SQLite 3 reference database
└── README.md                  # This build & execution guide
```

---

## Compilation & Run Guide

All components are written in standard C++17 and can be compiled using `g++` (GCC/Clang) or using `cmake`.

### 1. B-Tree Database Index

The B-Tree index component implements a generic templated key-value B-Tree supporting standard $O(\log n)$ search, sorted insertions, child-node splitting, and hierarchical tree visualization.

*   **Compile:**
    ```bash
    g++ -std=c++17 index/main.cpp -o btree_demo
    ```
*   **Run:**
    ```bash
    ./btree_demo
    ```

### 2. Cache Eviction Buffer Pool Manager (Clock Sweep)

The buffer pool manager simulates a database page/record cache using the thread-safe **Second-Chance (Clock Sweep)** replacement algorithm. It features a thread-safe implementation with a background maintenance thread.

*   **Compile:**
    ```bash
    g++ -std=c++17 storage_buffer/main.cpp -lpthread -o buffer_demo
    ```
*   **Run:**
    ```bash
    ./buffer_demo
    ```

### 3. In-Memory Transaction Manager

The Transaction Manager combines concurrency control primitives: MVCC snapshots for readers (no locking or blocking), Strict 2PL exclusive locks for writers (held until commit/abort), cycle-based waits-for deadlock detection, first-updater-wins serialization checks, and a vacuum/GC routine.

*   **Compile:**
    ```bash
    g++ -std=c++17 -Wall -Wextra lab8/main.cpp -o txn_demo
    ```
*   **Run:**
    ```bash
    ./txn_demo
    ```

### 4. SQL Lexer & AST Query Executor

A query parser that performs lexical tokenization of query strings, constructs an Abstract Syntax Tree (AST) representing the selection logic, and executes the queries against employee rows.

*   **Compile:**
    ```bash
    g++ -std=c++17 query_parser/app/main.cpp query_parser/app/lexer.cpp -o query_parser_demo
    ```
*   **Run:**
    ```bash
    ./query_parser_demo
    ```
