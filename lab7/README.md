# **Transaction Processing Manager**

**Author:** Patel Jash

**Roll No:** 24bcs10632

**Course:** ADBMS Lab 7

## **Overview**

This project is an in-memory transaction management simulator implemented in C++17. It explores a hybrid concurrency control mechanism similar to that used by robust DBMS systems like PostgreSQL. It merges **Multi-Version Concurrency Control (MVCC)** for read transactions with **Strict Two-Phase Locking (2PL)** for write transactions.

The manager operates in a single-threaded, deterministic space, allowing for precise and reproducible testing of intricate scenarios such as deadlocks, isolated reads, and concurrent write conflicts.

## **Core Mechanisms**

* **MVCC Snapshot Isolation:** A snapshot of the database is captured whenever a new transaction begins. Readers retrieve data based on this isolated snapshot, ensuring they are not obstructed by concurrent writers modifying the state.
* **Strict Two-Phase Locking (2PL):** Writers obtain exclusive row-level locks on records they intend to mutate. These locks are strictly maintained until the transaction either successfully commits or is aborted.
* **Deadlock Detection:** The manager keeps track of dependencies in a wait-graph. Upon detecting a cycle (deadlock), the system autonomously selects the most recent transaction (highest ID) as the victim, breaking the cycle through an abort.
* **First-Updater-Wins Logic:** During the commit phase, the system verifies if another concurrent transaction has already modified and committed the same record. If a conflict arises, the current transaction suffers a serialization error and is rolled back.
* **Vacuum Process (Garbage Collection):** A cleanup utility routinely sweeps the memory to eliminate obsolete versions of records that are no longer accessible by any active transaction's snapshot.

## **Source Files**

* transaction_engine.h: Declares the core structures, enumerations, and the main `TxManager` class.
* transaction_engine.cpp: Implements the engine's functionality, including lock acquisition, version tracking, deadlock breaking, and transaction lifecycle.
* main.cpp: The execution script. It runs five separate tests to prove the reliability of MVCC, 2PL, Deadlock handling, Serialization faults, and Vacuuming.
* Makefile: Simplifies the compilation process and execution.

## **Requirements**

* A C++17 compliant compiler (g++, clang++, etc.).
* make utility for build automation.

## **Build & Execute Instructions**

1. **Building the Application:**
   Navigate to the project directory via terminal and run:
   ```bash
   make
   ```
   This compiles the source code into an executable named `tx_engine_test`.

2. **Running the Suite:**
   You can run the output binary directly:
   ```bash
   ./tx_engine_test
   ```
   Alternatively, you can run the test suite via make:
   ```bash
   make run
   ```

3. **Cleaning Up:**
   To delete compiled object files and the executable, use:
   ```bash
   make clean
   ```

## **Test Cases (main.cpp)**

1. **Snapshot Consistency (MVCC):** Confirms that a reading transaction retains its original snapshot data, ignoring any new data committed by a concurrent writing transaction.
2. **Locking via Strict 2PL:** Validates that an exclusive write lock correctly blocks other writers from modifying the same record until the lock is freed.
3. **Deadlock Breaking:** Intentionally creates a circular wait condition between two transactions and checks if the engine identifies and aborts the younger transaction to resolve it.
4. **First-Updater-Wins Check:** Tries to overwrite a record concurrently and ensures it correctly aborts at the commit stage due to a mutation conflict.
5. **Garbage Collection (Vacuum):** Triggers the vacuum routine to confirm that memory is reclaimed by removing dead record versions while preserving live, visible ones.