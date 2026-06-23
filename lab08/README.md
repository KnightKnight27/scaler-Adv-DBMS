# **Transaction Processing Engine**

**Author:** Jatin Chulet

**Roll No:** 24bcs10213

**Course:** ADBMS Lab 6

## **Overview**

This project is a custom in-memory database transaction processing simulator written in C++17. It demonstrates a hybrid concurrency control model similar to PostgreSQL, combining **Multi-Version Concurrency Control (MVCC)** for read operations with **Strict Two-Phase Locking (2PL)** for write operations.

The engine simulates a single-threaded deterministic environment to reliably test complex transaction scenarios like deadlocks, concurrent overwrites, and isolated snapshots.

## **Key Features**

* **MVCC Snapshot Isolation:** Every transaction takes a snapshot of the database state upon creation. Readers fetch data against this snapshot, ensuring that reads are never blocked by concurrent writers.  
* **Strict Two-Phase Locking (2PL):** Writers acquire exclusive row-level locks on records. These locks are strictly held until the transaction either commits or rolls back.  
* **Deadlock Management:** The engine maintains a wait-for graph. When a cycle (deadlock) is detected, the youngest transaction (highest ID) is automatically chosen as the victim and rolled back to break the cycle.  
* **First-Updater-Wins Strategy:** At commit time, the engine checks if another transaction has already committed a modification to the same record. If a conflict is found, the current transaction is aborted with a serialization error.  
* **Garbage Collection (Vacuum):** A built-in cleanup process safely sweeps and removes outdated historical versions of records that are no longer visible to any active transaction.

## **Project Structure**

* transaction\_engine.h: Contains the core class definitions, data structures, and state enums for the engine.  
* transaction\_engine.cpp: The main implementation logic handling locks, versioning, deadlock detection, and commits.  
* main.cpp: The test runner. It executes five distinct scenarios to validate MVCC, 2PL, Deadlocks, Serialization Failures, and Garbage Collection.  
* Makefile: Automates the compilation and execution process.

## **Prerequisites**

* A C++17 compatible compiler (e.g., g++ or clang++).  
* make build automation tool.

## **How to Build and Run**

1. **Compile the Project:**  
   Open your terminal in the project directory and run:  
   make

   This will generate an executable named tx\_engine\_test.  
2. **Run the Tests:**  
   You can run the generated executable directly:  
   ./tx\_engine\_test

   Or use the make shortcut:  
   make run

3. **Clean Build Files:**  
   To remove the compiled objects and the executable, run:  
   make clean

## **Test Scenarios Covered (main.cpp)**

1. **MVCC Consistency:** Verifies that a reader continues to see its historical snapshot even after a concurrent writer commits new data.  
2. **Strict 2PL Locks:** Ensures an exclusive write lock forces subsequent writers on the same key into a waiting state until the lock is released.  
3. **Deadlock Resolution:** Creates a deliberate cyclic dependency between two transactions and asserts that the system successfully aborts the younger transaction.  
4. **First-Updater-Wins:** Attempts a concurrent overwrite and verifies it fails with a conflict error at the commit phase.  
5. **Garbage Collection:** Runs the vacuum function to ensure dead versions of records are successfully swept from memory while retaining live data.