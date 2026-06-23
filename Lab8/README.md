# Lab 8 — In-Memory Transaction Manager

**Student:** Praneeth Budati
**Roll Number:** 24BCS10081

---

## Objective

The objective of this lab is to implement a lightweight transaction manager in **C++17** that demonstrates several core database concurrency-control mechanisms in a simplified environment.

The implementation combines the following concepts into a single `TransactionManager`:

* Multi-Version Concurrency Control (MVCC)
* Strict Two-Phase Locking (2PL)
* Deadlock Detection using Waits-For Graphs
* Lost Update Prevention
* Garbage Collection of Obsolete Versions (`vacuum()`)

The project is intentionally single-threaded so that every scenario can be reproduced deterministically.

---

## Project Structure

```text
Lab8/
├── README.md
└── main.cpp

```

* `main.cpp` contains the complete transaction manager implementation and demonstration cases.
* `README.md` explains the design decisions and expected behavior.

---

## Compilation & Execution

Compile using a C++17-compatible compiler:

```bash
cd Lab_8_praneeth_24bcs10081

g++ -std=c++17 -Wall -Wextra -Wpedantic main.cpp -o txmgr

./txmgr
```

The program produces deterministic output. Instead of using real thread blocking, lock conflicts are reported as `BLOCKED`, allowing all scenarios to be demonstrated consistently.

---

# System Design

## 1. MVCC-Based Version Storage

Each key maintains a chain of versions rather than a single value.

```cpp
struct Version {
    int value;
    int creator;
    int invalidator;
};
```

Where:

* `value` stores the actual data.
* `creator` stores the transaction that created the version.
* `invalidator` stores the transaction that later replaced it.

When a transaction starts, it records the set of transactions that had already committed. This becomes its **snapshot**.

A version is visible only if:

* Its creator is visible to the transaction.
* Its invalidator is not visible to the transaction.

This allows readers to continue seeing a stable snapshot even when other transactions commit newer versions.

---

## 2. Strict Two-Phase Locking (2PL)

The lock manager supports:

* Shared Locks (S)
* Exclusive Locks (X)

### Compatibility Matrix

| Existing Lock | Requested S | Requested X |
| ------------- | ----------- | ----------- |
| S             | Allowed     | Wait        |
| X             | Wait        | Wait        |

Rules:

* Reads acquire shared locks.
* Writes acquire exclusive locks.
* Locks are released only during commit or abort.
* A transaction holding the only shared lock may upgrade directly to an exclusive lock.

MVCC determines visibility while locking prevents conflicting updates.

---

## 3. Deadlock Detection

Whenever a lock request cannot be granted immediately, the transaction is added to a waits-for graph.

Example:

```text
T1 waits for T2
T2 waits for T1
```

This creates a cycle.

The manager performs a DFS traversal on the graph whenever blocking occurs.

If a cycle is found:

1. The youngest transaction (largest transaction ID) is selected.
2. That transaction is aborted.
3. Its locks are released.
4. Waiting transactions continue.

This mimics common database deadlock resolution strategies.

---

## 4. Lost Update Prevention

After acquiring an exclusive lock, a transaction checks whether another transaction has already committed a newer version of the same key.

If a conflicting committed version exists outside the transaction's snapshot:

```text
Current transaction → Abort
```

This implements a **First-Updater-Wins** policy and prevents lost updates.

---

## 5. Vacuum Operation

Every update leaves behind an older version.

Over time these versions become unnecessary.

The `vacuum()` function removes versions whose invalidating transaction:

* Has already committed.
* Is older than the oldest active snapshot.

This behavior is inspired by PostgreSQL's garbage collection mechanism.

---

# Demonstration Scenarios

The program executes seven predefined scenarios.

| Scenario               | Purpose                                       |
| ---------------------- | --------------------------------------------- |
| Snapshot Isolation     | Demonstrates stable snapshot reads            |
| Shared Locks           | Shows multiple readers accessing the same key |
| Writer Blocking        | Demonstrates S/X lock conflicts               |
| Lock Upgrade           | Converts S lock to X lock                     |
| Deadlock Detection     | Detects and resolves a waits-for cycle        |
| Lost Update Prevention | Aborts conflicting stale writers              |
| Vacuum                 | Removes obsolete versions                     |

---

# Sample Output

```text
Lab 8 - in-memory transaction manager
(Praneeth Budati, 24BCS10081)

=== 1. Snapshot isolation ===
T2 BEGIN
T3 BEGIN
T3 writes new version
T3 COMMIT
T2 still reads old version

=== 2. Shared locks ===
Two readers acquire shared locks successfully

=== 3. Writer blocking ===
Reader holds S lock
Writer requests X lock -> BLOCKED
Reader commits
Writer proceeds

=== 4. Lock upgrade ===
Transaction upgrades S -> X

=== 5. Deadlock detection ===
Cycle detected
Youngest transaction aborted

=== 6. Lost update prevention ===
Concurrent stale writer aborted

=== 7. Vacuum ===
Dead versions removed
```

---

# Key Observations

### Snapshot Isolation

A transaction always observes the database state corresponding to its own snapshot, regardless of later commits.

### MVCC and Locking Serve Different Purposes

* MVCC determines which versions are visible.
* Locking controls when conflicting operations may proceed.

Both mechanisms are required for correctness.

### Deadlocks Are Graph Problems

Deadlock detection can be reduced to cycle detection in a waits-for graph.

### Version Garbage Collection Is Necessary

Without cleanup, version chains would continue growing indefinitely.

The vacuum process ensures storage remains manageable.

---

# Conclusion

This lab demonstrates how several important database concepts can be combined within a compact transaction engine. Although simplified, the implementation captures the core ideas used by production database systems such as PostgreSQL and MySQL.

Through MVCC, strict 2PL, deadlock detection, lost-update prevention, and vacuuming, the project provides a practical understanding of how modern databases maintain consistency while supporting concurrent transactions.

---

**Author:** Praneeth Budati
**Roll Number:** 24BCS10081
