# MiniDB — Team JustChill

A from-scratch relational database engine in modern C++17: paged storage, a
buffer pool, a B+ Tree index, a Volcano-model query executor, serializable
transactions via table-level Strict 2PL, write-ahead logging, and a
primary/replica replication layer.

**Chosen extension track: Track D — Distributed Systems (Replication).**

---

## Team

| Name | Roll Number | Scaler Email |
|------|-------------|--------------|
| Ansh Mahajan | 24BCS10345 | ansh.24bcs10345@sst.scaler.com |
| Prabhav Semwal | 24BCS10358 | prabhav.24bcs10358@sst.scaler.com |
| Pulasari Jai | 24BCS10656 | pulasari.24bcs10656@sst.scaler.com |
| Pranav Nayal | 24BCS10236 | pranav.24bcs10236@sst.scaler.com |

---

## Overview

MiniDB executes a small SQL surface (`SELECT … WHERE … JOIN`, `INSERT`,
`DELETE`) over durable, paged storage. A query flows top-to-bottom through five
layers:

1. **Parse** SQL text into query objects.
2. **Optimize** — if a `WHERE` clause targets the primary key, use an index
   scan; otherwise a full table scan. Joins are ordered left-deep.
3. **Execute** with a Volcano (iterator) operator tree.
4. **Index & store** rows through a B+ Tree and a buffer-pooled heap file.
5. **Make it durable & distributed** — WAL guarantees committed work survives a
   crash, and the primary ships those WAL records to a read replica.

See [docs/architecture.md](docs/architecture.md) for the full layered diagram
and per-component detail.

## Architecture at a glance

```
SQL → Parser → Optimizer → Volcano Executor ──┬─► LockManager (table-level 2PL)
                                              ├─► B+ Tree index (key → RID)
                                              └─► BufferPool → HeapFile → Page (4KB)
                                                        │
                                                        ▼
                                              WAL ──TCP──► Replica
```

## Repository layout

```
Team_JustChill/
├── CMakeLists.txt            # build: storage + query_engine libs, tests, benchmarks
├── README.md                 # this file
├── src/
│   ├── page.{h,cpp}          # 4KB page frame                       (Track 2)
│   ├── heap_file.{h,cpp}     # paged disk I/O                       (Track 2)
│   ├── buffer_pool.{h,cpp}   # LRU buffer pool                      (Track 2)
│   ├── btree.{h,cpp}         # B+ Tree primary-key index            (Track 3)
│   ├── execution.{h,cpp}     # Volcano operators + storage iface    (Track 3)
│   ├── lock_manager.{h,cpp}  # table-level Strict 2PL               (Track 3)
│   ├── transaction.{h,cpp}   # transaction state                    (Track 3)
│   ├── parser.{h,cpp}        # SQL → query objects                  (Track 1)
│   ├── optimizer.{h,cpp}     # index-vs-scan, join order            (Track 1)
│   ├── wal.{h,cpp}           # write-ahead logging                  (Recovery)
│   ├── replication.{h,cpp}   # primary/replica over TCP             (Track D)
│   └── main.cpp              # server entry (primary | replica)
├── tests/
│   ├── track3_test.cpp       # executor / B+ Tree / lock-manager unit tests
│   └── track1_2_test.cpp     # storage + WAL/replication unit tests
├── benchmarks/
│   ├── benchmark.cpp         # storage-layer perf benchmark (pages, cache)
│   └── query_benchmark.cpp   # query-layer INSERT/SELECT benchmark   (Track 4)
└── docs/
    └── architecture.md       # full architecture write-up
```

## Design decisions

| Decision | Rationale |
|----------|-----------|
| **Volcano (iterator) execution model** | `open/next/close` lets operators compose into arbitrary trees and stream tuples one at a time — no intermediate materialization, and new operators slot in without touching existing ones. |
| **B+ Tree for the primary index** | Sorted leaves chained in a linked list give O(log n) point lookups *and* efficient ordered range scans, which directly powers the `IndexScan` operator and range predicates. |
| **Tombstone deletes (no rebalancing)** | Deletes flag the leaf entry `is_deleted` instead of merging nodes. This keeps the index simple and delete latency constant; the tradeoff is that space isn't reclaimed until a (future) compaction. |
| **Table-level (not row-level) locking** | A coarse `unordered_map<table, holders>` gives correct serializable isolation with far less bookkeeping than row locks — the right complexity/throughput tradeoff for a teaching engine. |
| **Timeout-based deadlock handling** | A 3-second acquisition timeout that throws (→ abort) avoids building and cycle-checking a wait-for graph, while still guaranteeing no transaction blocks forever. |
| **Storage behind an interface** | The executor talks to a `Table`/`Catalog` contract, so the in-memory store used today and the page-backed store wired in later are interchangeable without changing operator code. |
| **WAL before page flush** | Logging mutations ahead of data pages is what makes committed transactions durable across crashes — and the same log stream is what we replicate. |

## Extension track: Distributed Systems (Replication) — justification

We chose **Track D** because our durability design already produces the exact
artifact a distributed system needs: an **ordered Write-Ahead Log**. That makes
**log-shipping replication** a natural, low-risk extension rather than a bolt-on:

- **Reuses the WAL.** The primary already serializes every committed mutation to
  the WAL for crash recovery. Replication simply streams those same records to a
  replica over TCP — one source of truth, two consumers (local recovery + remote
  apply).
- **Deterministic apply = consistency.** Because the replica replays the
  primary's log in commit order, it converges to an identical state, giving
  consistent **read replicas** that offload read traffic from the primary.
- **Clear demo of distributed-systems concepts.** A two-node primary/replica
  setup concretely exercises ordering, network transport, and read-after-write
  consistency — the core ideas the track is meant to teach — without the full
  complexity of consensus.

Run modes are selected on the command line (`main.cpp`): start a node as the
`primary` or as a `replica` pointed at the primary's address.

## Build

Requires CMake ≥ 3.12 and a C++17 compiler (tested with g++ 15.2, MinGW-w64).

```bash
cd MiniDB_Projects/Team_JustChill
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces:
- `storage` — storage + WAL/replication static library
- `query_engine` — query engine (B+ Tree, executor, 2PL) static library
- `track3_test` — query-engine unit tests
- `track1_2_test` — storage + WAL/replication unit tests
- `benchmark` — storage-layer performance benchmark
- `query_benchmark` — query-layer INSERT/SELECT benchmark
- `minidb` — server entry point (primary | replica)

> **Windows note:** `storage` (and therefore `benchmark`, `track1_2_test`,
> `minidb`) currently fails to compile on MinGW because `replication.cpp` uses
> POSIX sockets — see [Limitations](#limitations--known-issues) issue 1. The
> `query_engine`, `track3_test`, and `query_benchmark` targets build on all
> platforms; the full project builds on Linux/macOS.

## Test

```bash
cd build
ctest --output-on-failure        # runs track3_test and track1_2_test
```

`track3_test` exercises ~2,100 assertions across the B+ Tree (splits, range
scans, tombstone deletes), the lock manager (shared compatibility, the 3-second
exclusive timeout, lock upgrade, and blocked-waiter wakeup), and the full
operator set (scan / index / filter / project / join / insert / delete).
`track1_2_test` covers the storage layer and WAL/replication (Linux/macOS).

## Benchmark results

An exhaustive, systems-grade benchmark of MiniDB's query and storage engine layers. The numbers below reflect our implementation's performance bounds, network bottlenecks, and architectural trade-offs.

---

### I. The Experimental Setup (The Baseline)
* **Hardware Specifications:** Intel CPU with 20 cores (2.50GHz to 5.20GHz), 32 GB RAM, NVMe M.2 SSD storage.
* **OS Profile:** Fedora Linux
* **Network Topology:** Distributed nodes communicated over `127.0.0.1` (loopback TCP interface), representing local synchronous log shipping.
* **System Configuration:** Page size `4096` bytes, buffer pool capacity `10` frames (primary) / `3` frames (storage unit tests), global lock concurrency control.
* **Dataset Profile:** Initial state is a cold start (0 records). The database is pre-populated sequentially for reads/updates.

---

### II. Core Workload Benchmarks

The query engine was tested with a dataset size of $N = 10,000$ operations on a table with an integer primary key and text column.

| Workload Type | Description | Throughput | Avg Latency | p50 | p90 | p99 |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **Write Storm (INSERT)** | 10k sequential inserts on cold database | **534,238 QPS** | 1.87 μs | 1.94 μs | 2.22 μs | 4.84 μs |
| **Point Lookups (SELECT PK)** | 10k IndexScan descents for single keys | **2,215,010 QPS** | 0.45 μs | 0.43 μs | 0.50 μs | 0.81 μs |
| **Full Table Scans (SELECT \*)** | 100 scans traversing all 10,000 records | **1,253 QPS** | 797.55 μs | 791.12 μs | 848.67 μs | 1,125.97 μs |
| **Mixed CRUD (70/30)** | 70% Point Lookups / 30% INSERT statements | **1,295,855 QPS** | 0.77 μs | 0.58 μs | 1.18 μs | 2.29 μs |

---

### III. Storage Layer Performance

| Workload Type | Dataset Size | Duration | Throughput |
| :--- | :--- | :---: | :---: |
| **Sequential Page Allocations & Writes** | 1,000 pages | 0.0137 sec | **72,897 pages/sec** |
| **Cache Hit Read Latency (100% hits)** | 500,000 read ops | 0.4051 sec | **1,234,180 cache hits/sec** |
| **Random Mixed Operations (20% writes, triggers evictions)** | 10,000 ops | 0.0244 sec | **410,293 page ops/sec** |

---

### IV. Failure & Recovery Benchmarking

* **WAL Crash Recovery Speed:** Replaying a WAL file with 10,000 committed `INSERT` queries on startup:
  * **Total recovery time:** 13.73 ms
  * **Recovery throughput:** **728,342 rows/sec**
* **Failure Detection Time:** A transaction aborted on replica crash/timeout takes exactly **2.00 seconds** (bounded by the TCP socket `SO_RCVTIMEO` timeout).

---

### V. Distributed Replication & Systems Analysis (The "Insight")

#### 1. The Synchronous Replication Network Bottleneck
* **Workload:** `Replication Overhead (TCP loopback sendLog)`
* **Throughput:** **38,479 QPS** | **Avg Latency:** 25.99 μs | **p50:** 23.07 μs | **p90:** 31.55 μs | **p99:** 70.38 μs
* **Analysis:** Comparing a standalone `INSERT` write storm (1.87 μs average latency) with loopback replication (25.99 μs average latency) reveals a **~14x network latency penalty**. In synchronous replication, write throughput is strictly bound by the loopback network round trip time (RTT) and kernel socket processing overhead. Even over `localhost`, TCP handshakes/acknowledgments cap writes to ~38k QPS.

#### 2. The Global Lock Penalty
Using a coarse, single global lock (`global_db_lock`) ensures database state consistency during concurrent execution and background replication. The mixed CRUD workload throughput (**1,295,855 QPS**) represents sequentialized execution. While this prevents race conditions and pointer corruption during B+ tree splits, it caps concurrent query scalability.

#### 3. The I/O Cost of Durability
Writing transactions to the WAL and flushing to disk (`log_file.flush()`) before returning success guarantees ACID durability. Replaying these logged queries sequentially during startup allows the primary node to reconstruct its index and memory structures at **728,342 rows/sec**, showing that log replay is extremely fast once serialized, sequential disk reads are utilized.

#### 4. The Auto-Checkpoint Latency Spike
The automated checkpoint daemon runs every 5 minutes, locks the database, flushes dirty buffer pool pages, and truncates the WAL. This blocking checkpoint guarantees that crash recovery time is bounded. However, it will introduce a temporary latency spike (freezing transaction executions for a fraction of a millisecond) while the buffer pool flushes pages, demonstrating a classic systems engineering trade-off: trading short latency spikes for bounded recovery time.

## Limitations & Known Issues

We would rather document what we know is incomplete or fragile than ship it
silently. The following are real, understood limitations of the current code —
the "why" matters more than pretending they don't exist.

1. **`replication.cpp` does not build on Windows/MinGW.** It uses POSIX sockets
   (`<sys/socket.h>`, `<netinet/in.h>`, `<unistd.h>`, `socket`/`accept`/`recv`/
   `send`/`close`), which are not available on Windows. Because `replication.cpp`
   is part of the `storage` library, this breaks the `storage`, `benchmark`,
   `track1_2_test`, and `minidb` targets on MinGW. **It compiles and runs on
   Linux/macOS.** Cross-platform support needs a Winsock2 shim
   (`WSAStartup`/`closesocket`, link `ws2_32`) guarded by `#ifdef _WIN32`. The
   query engine (`query_engine`, `track3_test`, `query_benchmark`) is
   unaffected and builds everywhere.

2. **B+ Tree does not correctly handle duplicate keys spanning multiple leaves.**
   The index assumes a *unique* primary key. If the same key is inserted enough
   times to span more than one leaf, point `search` still returns a match, but a
   range scan can **under-count**: internal-node routing (`upper_bound`) jumps to
   the right-most leaf that could hold the key, so duplicate entries living in
   earlier leaves are skipped. Observed: 200 identical keys → `range(k, k)`
   returned only 40 rows. *Root cause:* leaf chaining is forward-only and the
   descent does not back up to the first leaf containing the key.

3. **No primary-key uniqueness enforcement.** `Table::insert` appends the row and
   adds an index entry unconditionally — a duplicate primary key is silently
   accepted, which then triggers issue (2). Uniqueness is currently assumed to be
   guaranteed by the caller, not checked by the engine.

4. **Query layer is in-memory only.** The executor runs on the in-memory
   `Table`/`Catalog` store and is **not yet bound to `BufferPool`/`HeapFile`**, so
   query-engine data is neither persisted to disk nor written to the WAL. The
   storage layer (Track 2) and the query layer (Track 3) are individually
   functional but not yet stitched together.

5. **Tombstone deletes never reclaim space.** `DELETE` flags rows/index entries
   `is_deleted` (by design — we skip rebalancing). Deleted entries are skipped by
   scans but remain in memory; there is no compaction/vacuum pass, so a
   delete-heavy workload grows unbounded.

6. **`NestedLoopJoin` is O(n×m) and equi-join only.** The inner child is
   re-scanned in full for every outer tuple, and only a single
   `outer[col] == inner[col]` equality predicate is supported — no hash join,
   merge join, or multi-column / inequality join conditions.

7. **Index is limited to a single integer primary key.** Text primary keys are
   not indexed (sequential scan only), and there are no secondary or composite
   indexes. `DELETE` therefore requires an integer primary key and throws
   otherwise.

8. **Concurrency is coarse and deadlock handling is approximate.** Locking is at
   **table granularity**, which serializes whole-table access. The 3-second
   acquisition timeout is a heuristic, not true deadlock detection — under heavy
   contention it can abort a transaction that was merely slow rather than
   genuinely deadlocked (a false positive).

9. **SQL coverage is limited to simplified inline parsing.** `main.cpp` parses
   basic `INSERT`, `DELETE`, and `SELECT` statements inline rather than through a
   full parser/optimizer module (`parser.cpp` / `optimizer.cpp` are not yet the
   query path). Richer SQL (multi-table JOINs, nested queries, expression
   predicates) is still constructed as operator trees in C++ rather than parsed
   from SQL text.

## Status & roadmap

- ✅ **Track 2 (storage)** and **Track 3 (index, executor, 2PL)** implemented, built under one CMake project, and tested.
- ✅ **Track 1** parser/optimizer (simplified inline parsing for INSERT, DELETE, and SELECT in `main.cpp`).
- ✅ **WAL** logging and **Track D** primary/replica synchronous replication.
- ✅ **Phase 3 Integration** complete (two-terminal primary/replica live verification and replication timeout handling).
- 🔜 Add a Winsock2 shim so `replication.cpp` builds on Windows (see issue 1).
- 🔜 Bind the executor's `Table` onto `BufferPool`/`HeapFile` (page-backed rows) and route mutations through the WAL.
- 🔜 Enforce primary-key uniqueness and fix duplicate-key range scans (issues 2–3).
