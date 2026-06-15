# Concurrency Control Engine (MVCC + 2PL)

This project implements a lightweight database engine focusing on concurrency control mechanisms. It guarantees serializability and resolves read/write contention by combining Multi-Version Concurrency Control (MVCC) with Strict Two-Phase Locking (2PL).

## Features

* **Object-Oriented Design:** The global state is encapsulated within a `DatabaseEngine` class, making the system modular and thread-safe.
* **MVCC:** Writers do not block readers. Reads fetch a consistent snapshot of the data based on the transaction's start time.
* **Strict 2PL:** Write operations acquire exclusive locks. Locks are released only upon transaction termination (commit or rollback), preventing cascading rollbacks.
* **Graph-based Deadlock Detection:** Implements a wait-for graph. Whenever a transaction blocks, a DFS cycle detection algorithm runs. If a cycle is found, a `DeadlockError` is thrown, aborting the youngest transaction.

## Installation & Usage

Requires `g++` and a C++17 compliant environment.

```bash
# Build the project
make all

# Run the test scenarios
make test

# Clean artifacts
make clean