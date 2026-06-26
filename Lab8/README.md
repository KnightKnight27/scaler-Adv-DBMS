# Lab 8 — Thread-Safe Multi-Version Concurrency Engine

**Student:** Yash Solanki

**Roll Number:** 24bcs10291

---

## Objective

The purpose of this lab is to architect and evaluate an advanced, high-performance in-memory transaction engine in **C++17** that implements strict isolation guarantees using concurrent design patterns.

The architecture unifies two dominant concurrency methodologies along with structural safety mechanisms into a singular `IsolationCoordinator` pipeline:

* **Multi-Version Concurrency Control (MVCC)** for version visibility management.
* **Strict Two-Phase Locking (S2PL)** with cross-thread condition synchronization.
* **Graph-Based Deadlock Resolution** using a deep Wait-For dependency graph analysis.
* **Thread-Safe Memory Management** utilizing system-level synchronization blocks (`std::mutex`, `std::condition_variable`).

Unlike purely synthetic simulations, this layout leverages thread primitives to model authentic runtime scheduling, conflict delays, and programmatic execution lifecycles.

---

## Project Structure

```text
Lab8/
├── README.md
└── mvcc.cpp

```

* `mvcc.cpp` Houses the structural primitives, global synchronization tables, MVCC record layers, S2PL engines, and runtime verification tracks.
* `README.md` Documenting system taxonomy, synchronization primitives, block mechanics, and operational specifications.

---

# System Design

## 1. Multi-Version Storage Architecture

Data structures avoid raw overwrites by processing data streams into individual historical updates. Each storage partition stores revisions managed by explicit lifetime boundary points:

```cpp
struct TupleRevision {
    std::string payload;
    TxnIdentifier insertion_tx;
    TxnIdentifier deletion_tx;
};

```

Key attributes include:

* `payload`: Represents the underlying string value.
* `insertion_tx`: The generating transaction id that minted this specific state.
* `deletion_tx`: The transaction identity tracking deletion or substitution bounds (defaults to `0` for live tracking records).

When a new transaction context instantiates, its transaction counter registers a visibility epoch snapshot identifier. Visibility logic evaluates whether an insertion is formally committed while ensuring that a deletion transaction is not yet visible within the running boundary scope.

---

## 2. Strict Two-Phase Locking (S2PL) Framework

Lock guarantees are isolated through an active tracking directory containing explicit conditional wait blocks (`std::condition_variable`). Access routines enforce state locks until final execution paths complete.

### Access Lock Matrix

| Active Mode State | Requested Shared | Requested Exclusive |
| --- | --- | --- |
| **Shared** | Granted Concurrent | Thread Suspended |
| **Exclusive** | Thread Suspended | Thread Suspended |

Design Rules:

* Read workflows evaluate across shared channels (`LockIntent::Shared`).
* Mutation workflows (`insert`, `update`, `delete`) command exclusive channels (`LockIntent::Exclusive`).
* Release hooks operate downstream, processing cleanups only inside final commit or abort execution phases.

---

## 3. Wait-For Cycle & Deadlock Mitigation

When access bounds collide, the requesting execution path records its dependency targets into a centralized graph matrix (`transaction_dependency_graph`).

```text
Txn A -> [Blocked By] -> Txn B
Txn B -> [Blocked By] -> Txn A (Cycle Identified)

```

The coordination engine invokes a structural Depth-First Search (DFS) check to identify transactional dependencies. If an evaluation cycle forms:

1. The transaction manager evaluates structural IDs inside the dependency chain.
2. The youngest process context (highest numeric value ID) is selected as the abort candidate.
3. The engine throws an explicit `ConcurrencyDeadlockException`, alerting caller blocks.
4. The system cleans up intermediate allocation queues and signals waiting execution threads.

---

## 4. Execution Recovery & Abort Strategies

When an update collision occurs or a deadlock condition triggers a rollback exception, the `IsolationCoordinator` drops transaction mutations via an structural cleanup sweep:

```text
Transaction Exception -> Purge Volatile Versions -> Discharge Allocation Locks -> Notify All Channels

```

The system steps through storage maps, purging active uncommitted rows where the candidate transaction matches the `insertion_tx` label, and un-allocates modification locks to safely advance other waiting worker queues.

---

# Verification Execution Scenarios

The program structures runtime pipelines to comprehensively test database execution behaviors:

| Scenario Module | Evaluation Objective |
| --- | --- |
| **Snapshot Isolation** | Confirms readers remain completely unaffected by uncommitted or post-snapshot data updates. |
| **Concurrent Shared Access** | Verifies multi-thread capability when parallel readers subscribe to an identical storage key. |
| **Exclusive Thread Blocking** | Assesses thread blocking when a write intent intercepts active reader operations. |
| **Deadlock Exception Handling** | Drives explicit cycle generation to verify automated youngest-transaction victim selection. |
| **Transactional Abort Recovery** | Verifies rollback precision across shared maps following execution failure paths. |

---

# Sample Output

```text
=========================================================
 DATABASE ENGINE TRANSACTION CONCURRENCY SIMULATOR       
=========================================================

[BEGIN] T1 created. Visibility Snapshot = { }
  [WRITE] T1 updated 'account_balance' <- '5000'
[COMMIT] T1 updates persisted to main storage catalog.

[BEGIN] T2 created. Visibility Snapshot = { T1 }
[BEGIN] T3 created. Visibility Snapshot = { T1 }
  [WRITE] T3 updated 'account_balance' <- '7500'
[Transaction ID: 3] Execution Committed Successfully
  [READ] T2 read 'account_balance' = '5000'
[Transaction ID: 2] Execution Committed Successfully

```

---

# Structural Key Observations

### Isolating vs. Sequencing

MVCC builds historical state views so that reading actions never block writing changes. Concurrently, S2PL sequences destructive mutations to prevent data conflicts. Together, they achieve consistent data execution.

### Deterministic Exception Traps

By relying on targeted exceptions (`ConcurrencyDeadlockException`), runtime logic safely exits deep call layers when thread blocks encounter deadlocks, allowing system states to clean up gracefully.

### Thread Synchronization Overheads

Protecting core properties with granular locks (`storage_heap_mutex`, `manager_graph_mutex`) prevents multi-threaded data corruption while letting independent keys lock and operate in parallel.

---

# Conclusion

This transaction layout shows how to combine MVCC tracking with thread-safe Strict 2PL. By using explicit resource locking channels, memory-mapped graphs, and modern exception structures, this design provides a scalable foundation for concurrent transaction managers like PostgreSQL and MySQL.