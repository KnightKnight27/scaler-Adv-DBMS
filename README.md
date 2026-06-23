# Advanced DBMS Lab Assignments

**Name:** Abhijit P
**Roll No:** 24BCS10175

## Overview

This repository contains the implementation of six Advanced Database Management Systems (DBMS) laboratory assignments. The assignments cover fundamental database internals, storage management, indexing structures, query processing, concurrency control, and transaction management concepts.

---

## Repository Structure

```text
scaler-Adv-DBMS/
│
├── Lab1/
├── Lab2/
├── Lab3/
├── Lab4/
├── Lab5/
└── Lab6/
```

---

## Assignments

### Lab 1 — C++ File I/O and System Call Analysis

Topics Covered:

* File I/O in C++
* Linux System Calls
* `strace` Analysis
* Virtual File System (VFS)
* Page Cache
* Inode Lookup Process

Key Concepts:

* Read and write operations
* System call tracing
* File system internals
* Storage access path

---

### Lab 2 — SQLite3 Internals and PostgreSQL Comparison

Topics Covered:

* SQLite Database Creation
* PRAGMA Commands
* Page Size Analysis
* Page Count Analysis
* Memory Mapping (`mmap`)
* Query Timing
* SQLite vs PostgreSQL Architecture

Key Concepts:

* Embedded databases
* Storage pages
* Database internals
* Server-based vs file-based databases

---

### Lab 3 — Clock Sweep Buffer Pool Replacement

Topics Covered:

* Buffer Pool Management
* Clock Sweep Algorithm
* Page Replacement Policies
* Cache Management

Key Concepts:

* PostgreSQL buffer manager
* Reference bits
* Cache eviction
* Memory management

---

### Lab 4 — Red-Black Tree and B-Tree

Topics Covered:

#### Red-Black Tree

* Balanced Binary Search Trees
* Rotations
* Recoloring
* Insertion

#### B-Tree

* Search
* Insert
* Split
* Borrow
* Merge
* Delete

Key Concepts:

* Database indexing
* Balanced trees
* Search optimization
* Storage-efficient structures

---

### Lab 5 — Expression Evaluation and SQL Parsing

Topics Covered:

#### Shunting Yard Algorithm

* Infix to Postfix Conversion
* Postfix Evaluation
* Operator Precedence
* Stack-based Parsing

#### SQL Parser

* SELECT Statements
* WHERE Clauses
* Query Tokenization
* Row Filtering using `vector<Row>`

Key Concepts:

* Query parsing
* Expression evaluation
* Basic query execution

---

### Lab 6 — Transaction Manager with MVCC and Deadlock Detection

Topics Covered:

#### MVCC

* Version Chains
* Multi-Version Concurrency Control

#### Strict 2PL

* Shared Locks
* Exclusive Locks
* Lock Management

#### Deadlock Detection

* Wait-For Graph
* Cycle Detection using DFS

#### Transaction Manager

* Begin Transaction
* Read
* Write
* Commit
* Abort

Key Concepts:

* Concurrency control
* Transaction processing
* Isolation mechanisms
* Deadlock handling

---

## Technologies Used

* C++
* SQLite3
* PostgreSQL
* Linux / WSL
* CMake
* STL Containers
* Multithreading
* File System Utilities

---

## Learning Outcomes

Through these assignments, the following database concepts were explored:

* Storage Engines
* File Systems
* Buffer Management
* Database Indexing
* Query Parsing
* Expression Evaluation
* Concurrency Control
* MVCC
* Two-Phase Locking
* Deadlock Detection
* Transaction Management

---

## Conclusion

These assignments provide practical implementations of core DBMS concepts, ranging from low-level storage internals to advanced transaction management mechanisms. Together, they demonstrate how modern database systems manage data storage, indexing, query execution, concurrency, and consistency.
