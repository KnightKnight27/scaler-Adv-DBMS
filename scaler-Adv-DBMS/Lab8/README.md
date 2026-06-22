# Lab 8: Concurrency Engine with MVCC, Strict 2PL, & Deadlock Detection

> **Subject:** Advanced Database Management Systems (ADBMS)  
> **Student Name:** Nandani Kumari  
> **Roll Number:** 24bcs10317  
> **Language Profile:** C++17  

---

## 1. Executive Summary

This laboratory project develops a mock thread-safe database transaction scheduler using C++17. It manages concurrent actions with three integrated modules:
1. **Multi-Version Concurrency Control (MVCC)**: Versions records using `xmin` and `xmax` parameters, ensuring readers access snapshots without blocking writers.
2. **Strict Two-Phase Locking (Strict 2PL)**: Imposed on write operations to ensure serializability. All locks are held until transaction commit or abort.
3. **DFS-Based Deadlock Graph Analyzer**: Constructs a Waits-For relation graph to detect cycles, throwing a `DependencyException` and aborting/rolling back the transaction.

---

## 2. Directory Contents

| Component | Description |
| :--- | :--- |
| `main.cpp` | Core implementation including heap records, lock table queues, Waits-For cycle resolver, and thread simulator. |
| `CMakeLists.txt` | Build configuration file specifying compile options for the binary `concurrency_evaluator`. |
| `README.md` | This design walkthrough and project summary. |

---

## 3. How to Compile & Run

To compile and launch the multi-threaded simulation scenarios:

```bash
# Configure and compile using CMake
cmake -S . -B build
cmake --build build

# Execute the test binary
./build/concurrency_evaluator
```

---

## 4. Concurrency Scenarios Simulated

The engine executes four distinct multithreaded test cases:
1. **Scenario 1: MVCC Snapshot Isolation**: Verifies that readers see historical values of `checking_amt` even when concurrently updated by write transactions.
2. **Scenario 2: Concurrent Shared Locks**: Assures that multiple reader transactions can hold shared locks concurrently.
3. **Scenario 3: Exclusive Lock + Waiting**: Simulates thread blockages where reader threads wait on condition variables for exclusive writer locks to release.
4. **Scenario 4: Deadlock Detection**: Triggers circular locks on resources `item_alpha` and `item_beta` using two threads. The engine detects the cycle and aborts one transaction.
