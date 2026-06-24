# MiniDB — Architecture

MiniDB is a teaching-grade relational engine built bottom-up: a paged storage
manager, a buffer pool, a B+ Tree index, a Volcano-style query executor, a
table-level lock manager for serializable transactions, write-ahead logging for
durability, and a primary/replica replication layer (our chosen extension
track).

## Layered data flow

```
                         SQL text
                            │
                            ▼
                  ┌───────────────────┐
                  │  Parser (Track 1) │  text → query objects
                  └─────────┬─────────┘
                            ▼
                  ┌───────────────────┐
                  │ Optimizer (Trk 1) │  PK in WHERE? → IndexScan : TableScan
                  └─────────┬─────────┘  left-deep join order
                            ▼
        ┌───────────────────────────────────────┐
        │      Execution engine (Track 3)        │  Volcano iterator model
        │  TableScan · IndexScan · Filter ·      │  open()/next()/close()
        │  Projection · NestedLoopJoin ·         │
        │  Insert · Delete                       │
        └───────┬───────────────────────┬────────┘
                │ acquire S / X locks    │ key lookups / range scans
                ▼                        ▼
   ┌────────────────────────┐  ┌────────────────────────┐
   │ LockManager (Track 3)  │  │  B+ Tree index (Trk 3)  │
   │ table-level Strict 2PL │  │  int64 key → RID        │
   │ 3s deadlock timeout    │  │  tombstone deletes      │
   └────────────────────────┘  └───────────┬────────────┘
                                            ▼
        ┌───────────────────────────────────────────────┐
        │            Storage layer (Track 2)             │
        │  BufferPool (LRU, pin/unpin, dirty flush)      │
        │  HeapFile (paged disk I/O)                     │
        │  Page (fixed 4 KB frames)                      │
        └───────────────────────┬───────────────────────┘
                                 │ mutations
                                 ▼
        ┌───────────────────────────────────────────────┐
        │  WAL (durability)  ──ship records over TCP──▶  │
        │  Replication (EXTENSION: Track D, Distributed) │
        │  primary appends WAL → replica applies → reads │
        └───────────────────────────────────────────────┘
```

## Components

### Storage layer — `page`, `heap_file`, `buffer_pool` (Track 2)
- **Page**: fixed 4 KB frame (`PAGE_SIZE = 4096`) carrying `page_id`,
  `pin_count`, `is_dirty`, and a raw byte buffer.
- **HeapFile**: page-granular disk I/O (`readPage`/`writePage`/`allocatePage`)
  over a single file, serialized by a per-file mutex.
- **BufferPool**: fixed set of in-memory frames with an LRU eviction list, a
  `page_id → frame` table, pin/unpin reference counting, and write-back of dirty
  pages on eviction. A single pool mutex makes it thread-safe.

### Index — `btree` (Track 3)
- B+ Tree mapping an `int64` primary key to a `RID` (record id).
- Internal nodes route; leaves hold key/RID pairs and are chained left-to-right
  for ordered range scans (used by `IndexScan`).
- Inserts split overflowing nodes and push separators up, creating a new root
  when needed, so the tree stays balanced and shallow.
- **Deletes are tombstones**: the leaf entry is flagged `is_deleted` and skipped
  by searches/scans. We deliberately skip merge/rebalancing on delete to keep
  the implementation small and predictable (see Design decisions in the README).

### Execution — `execution` (Track 3)
- **Volcano (iterator) model**: every operator exposes
  `open() / next(Tuple&) / close()`; parents pull tuples from children one at a
  time, so an operator tree streams without materializing intermediates.
- Operators: `TableScan`, `IndexScan`, `Filter`, `Projection`,
  `NestedLoopJoin` (left-deep equi-join), `Insert`, `Delete`.
- Storage is reached through a small `Table`/`Catalog` interface. Today that is
  an in-memory row store; the same contract (`insert / record(RID) /
  markDeleted / index()`) is what the page-backed Track 2 store will implement,
  so operator code does not change when the two are wired together.

### Concurrency — `lock_manager`, `transaction` (Track 3)
- **Strict Two-Phase Locking** at **table granularity** for serializable
  isolation. Shared (read) and Exclusive (write) modes with a Shared→Exclusive
  upgrade path; all locks are held until commit/abort and released together.
- Lock state lives in an `unordered_map<table, holders>`. A blocked request
  waits on a condition variable for up to **3 seconds**; on timeout it throws,
  which the transaction layer turns into an abort — a simple, deadlock-free
  policy without a wait-for graph.

### Durability — `wal` (Recovery)
- Write-Ahead Logging: mutations are appended to a log before pages are
  flushed, so committed transactions survive a crash and can be replayed during
  recovery.

### Extension track (Track D, Distributed Systems) — `replication`
- Primary/replica topology. The primary streams its WAL records to a replica
  over a TCP socket; the replica applies them in order to converge to the
  primary's state, providing consistent read replicas. (Rationale in README.)

## Current integration status
- Track 2 (storage) and Track 3 (index, executor, locks) are implemented,
  build under one CMake project, and are independently tested.
- The executor currently runs on the in-memory `Table` store; binding it onto
  `BufferPool`/`HeapFile` is the next integration step (page-backed `Table`).
- Parser/optimizer (Track 1), WAL, and replication are in progress.
