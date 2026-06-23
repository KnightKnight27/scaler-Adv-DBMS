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
│   └── track3_test.cpp       # executor / B+ Tree / lock-manager unit tests
├── benchmarks/
│   ├── benchmark.cpp         # Track 2 storage correctness tests
│   └── query_benchmark.cpp   # INSERT/SELECT throughput benchmark   (Track 4)
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
- `storage` — Track 2 static library
- `query_engine` — Track 3 static library
- `track3_test` — Track 3 unit tests
- `benchmark` — Track 2 storage correctness tests
- `query_benchmark` — Track 4 INSERT/SELECT benchmark

## Test

```bash
cd build
ctest --output-on-failure        # runs track3_test (executor / B+ Tree / locks)
./benchmark                      # Track 2 storage correctness tests
```

`track3_test` exercises ~2,100 assertions across the B+ Tree (splits, range
scans, tombstone deletes), the lock manager (shared compatibility, the 3-second
exclusive timeout, lock upgrade, and blocked-waiter wakeup), and the full
operator set (scan / index / filter / project / join / insert / delete).

## Benchmark results

`query_benchmark` loops **INSERT** and **SELECT 10,000 times** and reports
throughput and latency. Each operation runs through the real operator stack
(B+ Tree index updates, lock acquisition under table-level 2PL).

```bash
./build/query_benchmark 10000     # N defaults to 10000
```

Representative run — **Release build, Intel Core i5-13500H, Windows 11**,
in-memory `Table` store, N = 10,000:

| Operation | Total time | Throughput | Avg latency |
|-----------|-----------:|-----------:|------------:|
| `INSERT` (heap append + B+ Tree index update) | ~5.6 ms | ~1.78 M ops/sec | ~0.56 µs/op |
| `SELECT WHERE id = k` (IndexScan point lookup) | ~2.8 ms | ~3.57 M ops/sec | ~0.28 µs/op |
| `SELECT *` (full TableScan over 10k rows) | ~0.17 ms | ~57 M ops/sec | ~0.02 µs/op |

Observations:
- **Index point lookups are ~2× faster than inserts**, since inserts also pay
  for B+ Tree node updates/splits while lookups are read-only descents.
- **Full scans are an order of magnitude faster per row** than point lookups —
  sequential iteration over contiguous records has near-zero per-tuple overhead
  versus a fresh root-to-leaf descent per `SELECT`.
- Numbers are for the current in-memory store; figures will change once the
  executor is bound onto the buffer-pooled heap file (disk I/O + WAL).

## Status & roadmap

- ✅ **Track 2 (storage)** and **Track 3 (index, executor, 2PL)** implemented, built under one CMake project, and tested.
- ✅ **Track 1** parser/optimizer (simplified inline parsing for INSERT, DELETE, and SELECT in `main.cpp`).
- ✅ **WAL** logging and **Track D** primary/replica synchronous replication.
- ✅ **Phase 3 Integration** complete (two-terminal primary/replica live verification and replication timeout handling).
- 🔜 Bind the executor's Table onto BufferPool/HeapFile (page-backed rows).
