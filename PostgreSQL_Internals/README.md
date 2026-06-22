# PostgreSQL Internal Architecture

> Advanced DBMS · System Design Discussion · Topic 2

---

## 1. Introduction

Modern database systems are forced to balance several competing requirements. They must provide high concurrency so multiple users can access data simultaneously, guarantee durability even in the event of sudden crashes, maintain fast access despite data residing on slower storage devices, and efficiently locate records within massive datasets.

These goals often conflict. Immediate persistence improves reliability but hurts performance. Long-lived transaction snapshots improve consistency but make reclaiming storage more difficult. Aggressive caching accelerates access but complicates synchronization with disk.

PostgreSQL's architecture is particularly interesting because of the specific compromises it makes to resolve these tensions. Its roots trace back to the POSTGRES research project at the University of California, Berkeley, led by Michael Stonebraker during the 1980s. One of the project's most influential ideas was a **no-overwrite storage strategy**: rather than modifying records directly, updates create new versions while preserving older ones.

This design decision has far-reaching consequences. It motivates PostgreSQL's Multi-Version Concurrency Control (MVCC) system, explains the existence of VACUUM, allows readers and writers to operate concurrently, and requires transaction metadata to be stored alongside every tuple.

The architecture can largely be understood through four major subsystems:

* **Buffer Manager** – manages in-memory caching of disk pages.
* **B-Tree Indexes** – PostgreSQL's primary indexing mechanism.
* **MVCC** – the concurrency model based on multiple tuple versions.
* **Write-Ahead Logging (WAL)** – the durability and crash-recovery framework.

Together, these components provide PostgreSQL's balance between performance, consistency, concurrency, and reliability.

---

## 2. System Architecture Overview

PostgreSQL follows a process-per-connection architecture. A central supervisor process, commonly called the postmaster, accepts client connections and spawns dedicated backend processes to serve them.

Although backend processes are isolated from one another, they cooperate through a shared memory region created when the server starts. This shared area contains:

* The buffer cache (`shared_buffers`)
* WAL buffers
* Lock-management structures
* Transaction visibility metadata

A query interacts with several layers of the system before producing a result.

1. SQL text is parsed and transformed into an internal representation.
2. The optimizer evaluates possible execution plans using collected statistics.
3. The executor retrieves data through table and index access methods.
4. MVCC visibility checks determine which tuple versions are visible.
5. Data pages are obtained through the buffer manager.
6. Any modifications generate WAL records before pages are eventually written to disk.

A simplified view of the flow is:

```
SQL Query
    │
 Parser
    │
 Planner
    │
 Executor
    │
 ┌──┴──────────────┐
 │                 │
Heap Access    B-Tree Access
 │                 │
 └──────┬──────────┘
        │
       MVCC
        │
 Buffer Manager
        │
 ┌──────┼───────────┐
 │      │           │
 WAL  BgWriter  Checkpointer
        │
       Disk
```

Several important relationships emerge:

* Executors never read disk files directly.
* All page access passes through the buffer manager.
* WAL records must reach stable storage before modified pages can be written.
* MVCC participates in every tuple access.
* B-tree indexes rely on the same page cache and WAL infrastructure as heap storage.

---

## 3. Core Components

### Buffer Manager

The buffer manager serves as PostgreSQL's primary caching layer. Instead of reading pages directly from storage for every request, frequently used pages are kept in memory inside the shared buffer pool.

The cache is composed of fixed-size 8 KB frames allocated at startup. Each frame can hold one database page.

Three major structures support this mechanism:

#### Buffer Pool

The buffer pool contains the actual cached page contents.

#### Buffer Descriptors

Each cached page has a descriptor storing metadata such as:

* The page identity
* Reference count (pin count)
* Dirty status
* Usage information
* Synchronization metadata

#### Buffer Mapping Table

A hash table maps page identifiers to buffer frames, enabling PostgreSQL to determine quickly whether a requested page is already cached.

### Page Retrieval

When a backend requests a page:

1. PostgreSQL checks the mapping table.
2. If the page is already cached, the buffer is pinned and returned.
3. If absent, a victim frame is selected.
4. Dirty victims are flushed if necessary.
5. The required page is loaded from disk.

This process minimizes expensive storage operations by maximizing cache hits.

### Clock-Sweep Replacement

Instead of maintaining an exact Least Recently Used (LRU) list, PostgreSQL uses a clock-sweep algorithm.

Each buffer stores a small usage counter. Frequently accessed pages continually refresh their counters, while inactive pages gradually decay toward zero.

A circular "clock hand" scans the cache looking for unpinned pages with low usage counts that can be reclaimed.

Compared to true LRU, this approach sacrifices some precision but dramatically reduces contention in highly concurrent systems.

---

### B-Tree Indexes

PostgreSQL's default indexing mechanism is a B+ tree implementation based on the Lehman-Yao concurrent B-tree design.

All actual index entries reside in leaf pages, while internal pages contain routing information used to navigate the tree.

Each page contains:

* Search keys
* Heap tuple references
* High-key boundaries
* Links to neighboring pages

The linked leaf structure enables efficient range scans.

### Searching

A lookup begins at the root page and repeatedly chooses child pages until reaching the appropriate leaf node.

Since each level narrows the search space, lookup complexity remains logarithmic relative to index size.

### Concurrent Splits

When a page becomes full, PostgreSQL splits it into two pages.

The Lehman-Yao design uses sibling links and high keys to allow searches to continue safely even while splits are occurring. If a search lands on a page whose key range no longer contains the target value, it can follow the right sibling link and continue.

This eliminates the need for large-scale locking during structural modifications.

---

### Multi-Version Concurrency Control (MVCC)

MVCC is PostgreSQL's mechanism for providing concurrency without forcing readers and writers to block each other.

Rather than updating rows directly, PostgreSQL creates new row versions whenever modifications occur.

Each tuple stores metadata including:

* `xmin` – transaction that created the tuple
* `xmax` – transaction that invalidated the tuple
* `ctid` – location of the current or next tuple version
* Visibility flags

### Updates Create New Versions

An UPDATE operation is effectively implemented as:

1. Mark old version obsolete.
2. Insert a new version.

The original tuple remains on disk until it is no longer visible to any active transaction.

This design allows readers to continue using old versions while writers generate new ones.

### Snapshots

Every transaction operates using a snapshot describing which transactions are visible.

A tuple becomes visible when:

* Its creating transaction committed before the snapshot.
* Its deleting transaction is either absent or not visible within that snapshot.

Because each transaction has its own visibility rules, readers can proceed without interfering with writers.

### VACUUM

The downside of MVCC is the accumulation of obsolete row versions.

VACUUM periodically:

* Reclaims storage occupied by dead tuples.
* Updates free-space metadata.
* Maintains visibility information.
* Prevents transaction-ID wraparound issues.

Without VACUUM, table growth would continue indefinitely.

---

### Write-Ahead Logging (WAL)

WAL provides PostgreSQL's durability guarantees.

Every change made to database pages first generates a WAL record describing the modification.

Each record receives a Log Sequence Number (LSN), which uniquely identifies its position within the WAL stream.

Database pages also store the LSN corresponding to their most recent modification.

### The Write-Ahead Rule

Before a modified page can be written to disk, all WAL records describing changes to that page must already be persisted.

This ensures recovery information always exists if a crash occurs.

### Fast Commit Processing

At commit time PostgreSQL:

1. Writes a commit record into WAL.
2. Flushes WAL to stable storage.
3. Confirms success to the client.

Actual data pages may remain only in memory.

This is efficient because WAL writes are largely sequential, whereas data-page writes are random and scattered.

### Checkpoints and Recovery

Checkpoints periodically flush dirty pages and record a recovery starting point.

Following a crash:

1. PostgreSQL locates the latest checkpoint.
2. WAL records after that checkpoint are replayed.
3. Pages are restored to their most recent committed state.

Because WAL contains every modification in order, the same mechanism also supports replication by streaming WAL records to standby servers.

---

## Key Architectural Insights

Several themes appear repeatedly throughout PostgreSQL's design:

* Work is shifted away from latency-sensitive operations and into background maintenance.
* Concurrency is prioritized even when it introduces additional storage overhead.
* Sequential I/O is preferred over random writes whenever possible.
* Scalability is often favored over theoretically optimal algorithms.
* Many subsystems exist specifically to support the no-overwrite MVCC model.

The result is a database that performs well under heavy concurrent workloads while maintaining strong consistency and durability guarantees, even though those benefits require ongoing background work such as checkpointing, WAL management, and VACUUM processing.

