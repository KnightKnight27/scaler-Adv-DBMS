# Lab 8: Concurrency Engine with MVCC, Strict 2PL, & Deadlock Detection

> **Subject:** Advanced Database Management Systems (ADBMS)  
> **Student Name:** Vishudh Goel  
> **Roll Number:** 24bcs10162  
> **Language:** C++17  

---

## 1. Overview & Mechanics

This lab project features a thread-safe implementation of an in-memory SQL transaction scheduler. It coordinates concurrent actions using:
1. **Multi-Version Concurrency Control (MVCC)**: Versions records using `xmin` (creator) and `xmax` (deleter) parameters, ensuring readers access snapshots without blockages.
2. **Strict Two-Phase Locking (Strict 2PL)**: Acquires locks during transaction execution and releases them all at commit or abort.
3. **DFS-Based Deadlock Cycle Detector**: Discovers circular locks in a Waits-For relation graph and throws a `CircularWaitException` to abort the offending transaction.

---

## 2. Directory Contents

| Component | Responsibility |
| :--- | :--- |
| `main.cpp` | Complete concurrency engine with versioning history, lock manager, deadlock resolver, and thread execution tests. |
| `CMakeLists.txt` | Standard project configuration compiling the target binary `concurrency_scheduler`. |
| `README.md` | This technical manual and explanation. |

---

## 3. How to Compile & Run

To compile and launch the multi-threaded simulation scenarios:

```bash
# Configure and build the project
cmake -S . -B build
cmake --build build

# Execute the binary
./build/concurrency_scheduler
```

---

## 4. Concurrency Scenarios Simulated

The engine executes four distinct multithreaded test cases:
1. **Scenario 1: MVCC Snapshot Isolation**: Verifies that readers see historical versions of `resource_val` even when concurrently updated by write transactions.
2. **Scenario 2: Concurrent Shared Locks**: Assures that multiple reader transactions can hold shared locks concurrently.
3. **Scenario 3: Exclusive Lock + Waiting**: Simulates thread blockages where reader threads wait on condition variables for exclusive writer locks to release.
4. **Scenario 4: Deadlock Detection**: Triggers circular locks on resources `res_X` and `res_Y` using two threads. The engine detects the cycle and aborts one transaction.