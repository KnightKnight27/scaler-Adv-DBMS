# Concurrency Control: Two-Phase Locking (2PL) vs. Multi-Version Concurrency Control (MVCC)

Concurrency control is the database subsystem that ensures transactions can execute concurrently while maintaining the illusion of isolated execution (**Isolation** in ACID). The gold standard of isolation is **Serializability**, which guarantees that the concurrent execution of a set of transactions yields the same database state as some purely serial execution of those same transactions.

Two primary paradigms dominate concurrency control: **Two-Phase Locking (2PL)** (a pessimistic, lock-based approach) and **Multi-Version Concurrency Control (MVCC)** (a version-based, multi-view approach).

---

## 1. Two-Phase Locking (2PL)

2PL is a pessimistic concurrency control protocol. It assumes that conflicts are highly likely and uses locks to block transactions from accessing data that other transactions are modifying.

### 1.1 The Two Phases
To guarantee conflict serializability, a transaction must acquire and release locks in two distinct, monotonic phases:

1. **Growing Phase**: The transaction may acquire locks but cannot release any locks.
2. **Shrinking Phase**: The transaction may release locks but cannot acquire any new locks.

```
  Number of Locks
        ^
        |         Lock Point (Peak)
        |             /\
        |            /  \
        |  Growing  /    \  Shrinking
        |  Phase   /      \ Phase
        |         /        \
        +--------/----------\--------> Time
```

*Theorem: If every transaction in a schedule follows the 2PL protocol, the schedule is conflict serializable.*

### 1.2 Variants of 2PL
- **Basic 2PL**: Transactions can release locks at any time during the shrinking phase. This can lead to **Cascading Aborts** (if Tx1 aborts, any Tx2 that read data modified by Tx1 must also abort).
- **Strict 2PL (S2PL)**: The transaction holds all **Exclusive (X)** locks until it commits or aborts. Shared (S) locks can be released during the shrinking phase. This completely prevents cascading aborts.
- **Rigorous 2PL (SS2PL)**: The transaction holds **all** locks (both Shared and Exclusive) until it commits or aborts. The shrinking phase is compressed into the transaction commit. This is the most common lock-based protocol because it is easy to implement and guarantees strict serializability.

### 1.3 Lock Manager Internals
The DBMS maintains a global **Lock Manager** in memory, containing a hash table of **Lock Heads** hashed by data resource IDs (tuples, pages, or tables).

```
  Resource Hash Table:
  [Tuple A] ---> Lock Head ---> [Tx 1 (Hold: Exclusive)]
  [Tuple B] ---> Lock Head ---> [Tx 2 (Hold: Shared)] ---> [Tx 3 (Hold: Shared)] ---> [Tx 4 (Wait: Exclusive)]
```

Each Lock Head has a compatibility matrix. Shared locks are compatible with other Shared locks, but Exclusive locks are incompatible with any other locks.

### 1.4 Deadlocks
Because 2PL blocks transactions, it can lead to **Deadlocks** (e.g., Tx1 holds A, waits for B; Tx2 holds B, waits for A).
- **Deadlock Detection**: The DBMS maintains a **Wait-For Graph (WFG)** in the background. Nodes represent transactions, and directed edges represent lock waits. A background thread runs cycle detection (e.g., Tarjan's DFS algorithm) periodically. If a cycle is found, a transaction is selected as a "victim" and aborted.
- **Deadlock Prevention**: Transactions are assigned priority timestamps when they start (older transactions have higher priority).
  - **Wait-Die**: If an older transaction requests a lock held by a younger transaction, the older waits. If a younger transaction requests a lock held by an older transaction, the younger dies (aborts and restarts).
  - **Wound-Wait**: If an older transaction requests a lock held by a younger transaction, the older "wounds" the younger (forcing it to abort). If a younger transaction requests a lock held by an older transaction, the younger waits.

---

## 2. Multi-Version Concurrency Control (MVCC)

MVCC is an optimistic design paradigm based on a simple philosophy:
> **Readers should never block Writers, and Writers should never block Readers.**

Instead of updating a data tuple in-place and locking it, MVCC creates a new physical version of the tuple on every write.

### 2.1 The Conceptual Model
Each tuple contains metadata fields:
- `xmin` / $T_{create}$: The transaction timestamp or ID that created this version.
- `xmax` / $T_{end}$: The transaction timestamp or ID that deleted or replaced this version (initially null/empty).
- `pointer`: A reference to the next/previous version of this tuple, forming a version chain.

```
  Version Chain for Tuple 45 (Newest to Oldest):
  +----------------------+      +----------------------+      +----------------------+
  | Val: "Charlie"       |      | Val: "Bob"           |      | Val: "Alice"         |
  | xmin: 150, xmax: inf | ---> | xmin: 120, xmax: 150 | ---> | xmin: 100, xmax: 120 |
  +----------------------+      +----------------------+      +----------------------+
```

### 2.2 Read Views (Snapshot Isolation)
When transaction $T_{read}$ begins, it is assigned a snapshot of active transaction IDs. When $T_{read}$ reads a tuple, it traverses the version chain and selects the version where:
1. The creator ($xmin$) has committed and $xmin \le T_{read}$.
2. The deleter ($xmax$) has not committed, or $xmax > T_{read}$ (or is null).

This allows $T_{read}$ to read a consistent snapshot of the database without acquiring any locks, even if another transaction is writing a new version of the same tuple concurrently.

### 2.3 Version Storage Layouts
There are three main strategies for storing version chains:

1. **Append-Only (PostgreSQL)**:
   - All versions of a tuple are stored in the same main table space (heap).
   - Pros: Simple table scan logic; index pointers point to version records.
   - Cons: Massive write amplification; index pointers must be updated for every version update, creating "write bloat" on indexes (mitigated by HOT - Heap-Only Tuple optimization).

2. **Time-Split / Rollback Segments (MySQL InnoDB, Oracle)**:
   - The main table space only contains the *current* version of the data.
   - Older versions are converted into delta records and written to a separate **Rollback Segment** (Undo Log).
   - Pros: Fast scans on current data; index structures do not change on updates.
   - Cons: Reconstructing old versions for long-running readers requires sequentially applying deltas backward, which is slow.

3. **Delta Storage (HyPer)**:
   - The main table holds the current version.
   - A separate in-memory delta buffer stores only the modified fields (deltas).

### 2.4 Garbage Collection (GC) / Vacuuming
Because MVCC continuously creates new versions, the database will eventually run out of space if old versions are not purged.
- **Tuple-Level GC (Cooperative)**: When a thread scans a page and notices a version whose $xmax$ is older than the oldest active transaction in the system, it marks the slot as free.
- **Background Vacuuming (PostgreSQL)**: A dedicated daemon thread scans the table heap to locate dead versions, reclaim space, and update index pointers.

---

## 3. Head-to-Head Comparison

| Dimension | Two-Phase Locking (2PL) | Multi-Version Concurrency Control (MVCC) |
| :--- | :--- | :--- |
| **Basic Strategy** | Pessimistic (In-place updates with blocking). | Optimistic (Multi-versioning with non-blocking reads). |
| **Read-Write Interaction** | Readers block Writers; Writers block Readers. | Readers and Writers never block each other. |
| **Write-Write Interaction** | Writers block Writers (via Exclusive locks). | Writers block/abort Writers (First-Committer-Wins rule). |
| **Throughput (Read-Heavy)** | Low under contention (Readers queue behind Writers). | Extremely High (Fully concurrent non-blocking reads). |
| **Throughput (Write-Heavy)** | Good (locks queue transactions systematically). | High abort rate or latch contention on version chains. |
| **Storage Overhead** | Low (Updates are done in-place; WAL is linear). | High (Version bloat, requires background GC/Vacuuming). |
| **Deadlocks** | Possible (Requires WFG detection or prevention). | Rare/None (unless using pessimistic locks on updates). |
| **Implementation Complexity**| Low to Moderate (Lock Manager, Latch upgrades). | Extremely High (Version chains, GC, Snapshot tracking). |

---

## 4. Architectural Summary

- **Choose 2PL** if the workload consists of short, highly-contentious write transactions (OLTP) where conflict rates are very high, and storage overhead must be minimized.
- **Choose MVCC** for modern mixed workloads (HTAP) or read-heavy applications, where reporting queries (long-running reads) must run concurrently with transaction updates without blocking or causing deadlocks.
