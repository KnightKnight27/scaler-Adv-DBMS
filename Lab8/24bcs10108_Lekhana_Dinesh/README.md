# Lab 8 - In-Memory Transaction Manager

Name: Lekhana Dinesh  
Roll No: 24BCS10108

## 1. Objective

This lab builds a small in-memory transaction manager in C++17. The goal is to demonstrate how database ideas such as MVCC, strict two-phase locking, deadlock detection, commit/abort handling, and vacuum cleanup can work together in a simple key-value store.

## 2. Folder structure

```text
Lab8/24bcs10108_Lekhana_Dinesh/
├── README.md
└── transaction-manager/
    ├── README.md
    ├── Makefile
    └── main.cpp
```

## 3. Main concepts implemented

- Transaction begin, read, write, commit, and abort
- MVCC snapshot reads using version chains
- Strict 2PL for write locks
- Waits-for graph based deadlock detection
- Rollback of uncommitted writes
- Vacuum cleanup of old expired versions

## 4. How to compile and run

Detailed explanation is available in `transaction-manager/README.md`.

```bash
cd Lab8/24bcs10108_Lekhana_Dinesh/transaction-manager
make
make run
make clean
```

Direct compilation is also possible:

```bash
g++ -std=c++17 -Wall -Wextra -pthread main.cpp -o tx_manager
./tx_manager
```

## 5. Demo scenarios

The program runs six scenarios:

1. Basic transaction commit
2. MVCC snapshot isolation
3. Strict 2PL write lock conflict
4. Abort rollback
5. Deadlock detection
6. Vacuum cleanup

Each scenario prints the transaction steps and ends with a PASS message when the expected behavior is observed.

