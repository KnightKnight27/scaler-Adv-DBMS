# MiniDB System Architecture

## Design Goals

- Preserve the baseline MiniDB storage, parser, optimizer, executor, and strict 2PL flow instead of replacing them
- Keep physical storage and logical query processing small enough to explain end to end
- Add durability with a clear WAL-before-data invariant
- Add Track B MVCC as a second transaction mode that can be benchmarked directly against 2PL
- Keep demos and tests deterministic enough for capstone evaluation

## Non-Goals

- Full SQL compatibility
- Production-grade ARIES recovery
- Distributed execution or replication
- Background vacuum, compaction, or online index maintenance
- Highly tuned cost estimation or hardware-specific benchmarking

## End-To-End Data Flow

```text
SQL text
  -> parser
  -> typed statement
  -> optimizer
  -> physical plan
  -> executor
  -> transaction hooks
  -> storage/index access
  -> result rows or status
```

The parser and optimizer remain mode-agnostic. The transaction manager decides whether a request follows the strict 2PL path or the MVCC path, while the storage layer remains page-based in both cases. This separation keeps the extension focused on concurrency control and recovery rather than turning Track B into a new engine.

## Storage Architecture

### Page Layout

`pages.py` defines fixed-size `4096` byte slotted pages. Each page contains:

- a small header with slot count and free-space boundary
- a slot directory with fixed-size entries
- variable-length JSON row payloads packed from the end of the page backward

This layout gives the engine stable physical addresses and simple append-within-page insertion semantics. Physical identity is represented as `RecordID(page_id, slot_id)`.

### Heap Files

Each table is stored as a heap file managed by `PageManager`. Page ids map directly to page offsets in the underlying file, which keeps the persistence model easy to reason about:

1. Allocate a new page when no existing page can fit the row payload.
2. Read and modify a page in memory through the buffer pool.
3. Write the page back through `PageManager` once it is safe to flush.

### Core Storage Invariants

- Page size is fixed across the engine.
- A `RecordID` remains a physical anchor even when the logical key also participates in MVCC versioning.
- Page flushes must happen only after the required WAL records have been flushed.
- Logical recovery may rebuild physical heap state, so physical placement is not the only durable truth.

## Buffer Pool Lifecycle

`BufferPoolManager` adds a small but explicit cache between logical operations and disk pages.

1. `fetch_page` checks the in-memory cache first.
2. A cache miss reads the page from `PageManager`.
3. The page is pinned while in active use.
4. `unpin_page` decrements the pin count and marks the page dirty when needed.
5. LRU chooses an evictable victim among unpinned pages only.
6. Dirty victims are flushed before eviction.

The buffer pool is intentionally simple, but it still demonstrates the concepts expected in a systems project: page residency, pinning, dirty tracking, and eviction policy. Debug logs also expose hit, miss, flush, and eviction events for tests and demos.

## Index Lookup Path

`index.py` implements the B+ Tree used for primary-key access. The important architectural point is that the index is a logical-key locator, not a visibility engine.

Lookup path for a primary-key read:

1. The optimizer sees an indexed equality predicate and emits `INDEX_SCAN`.
2. The executor uses the B+ Tree to find the `RecordID` anchor for that logical key.
3. The transaction layer applies the active visibility rule:
   - strict 2PL reads the currently committed logical row while holding a shared lock
   - MVCC resolves the newest version visible to the transaction snapshot

This ordering is deliberate: Track B extends the visibility model without discarding the baseline access-path story.

## Optimizer Plan Selection

`optimizer.py` uses compact catalog statistics to choose among a few physical operators.

### Single-Table Reads

- If an indexed predicate exists on the requested column, the plan is `INDEX_SCAN`.
- Otherwise the plan is `TABLE_SCAN`.
- Equality selectivity for indexed primary-key access is estimated as `1 / row_count`.
- Unknown predicates fall back to selectivity `0.1`.

### Joins

- The join operator is `NESTED_LOOP_JOIN`.
- The smaller estimated relation becomes the outer input.
- Both children are currently modeled as scans, which keeps plan generation simple and predictable.

This is not meant to rival a production optimizer, but the rules are transparent, reproducible, and visible through `EXPLAIN`.

## 2PL Concurrency Path

Strict 2PL is the default mode and the baseline that Track B must preserve.

1. `BEGIN` creates a transaction record.
2. `before_read` requests a shared lock.
3. `before_write` requests an exclusive lock.
4. Locks are held until `COMMIT` or `ROLLBACK`.
5. End-of-transaction cleanup releases all held locks.

### Locking Invariants

- Shared locks are compatible only with other shared locks.
- Exclusive locks conflict with both shared and exclusive locks.
- Strictness means locks are not released early.
- Reads can block behind conflicting writers.

### Deadlock Detection

The lock manager builds a waits-for graph whenever a transaction must wait. A cycle means a deadlock. The implementation chooses the youngest transaction in the cycle as the victim and aborts it, which is deterministic enough for demo and test coverage.

## WAL And Recovery Path

Durability is handled through `WALManager` and `RecoveryManager`.

### WAL Path

1. A transaction begins and writes a `BEGIN` record.
2. Each logical insert or delete appends a corresponding WAL record.
3. Commit appends a `COMMIT` record with commit ordering metadata.
4. `storage.py` calls `before_page_flush` so the log is flushed before any dirty page reaches disk.

### Recovery Path

1. Startup scans the append-only WAL.
2. The recovery manager separates committed transactions from incomplete ones.
3. Committed operations rebuild the version history.
4. The current logical rows are materialized back into heap storage.
5. Uncommitted work is ignored or discarded.

### Recovery Scope

This is logical recovery, not ARIES. There are no page LSNs, compensation log records, or physiological redo/undo phases. The benefit is a smaller design that still proves the required durability property for the project.

## MVCC Visibility Algorithm

Track B introduces `VersionStore` and `MVCCManager` while keeping the strict 2PL baseline available.

### Version Model

Each version stores:

- logical key
- row payload
- creating transaction id
- begin or commit timestamp
- tombstone state for deletes
- committed flag

### Snapshot Rules

1. A transaction receives `snapshot_ts` at `BEGIN`.
2. Commits receive a monotonically increasing commit timestamp.
3. The transaction sees its own staged writes immediately.
4. It does not see uncommitted versions written by other transactions.
5. Among committed versions, it sees the newest version whose begin timestamp is not newer than the snapshot.
6. If that newest visible version is a tombstone, the row is invisible.

### Why Readers Do Not Block Writers

In MVCC mode, readers do not acquire shared locks for visibility. They read from their snapshot and only rely on the version chain. Writers still coordinate through transaction hooks, but readers no longer wait behind the writer's exclusive lock window for the same logical key.

## Transaction Mode Comparison

| Aspect | Strict 2PL | MVCC |
| --- | --- | --- |
| Read path | Shared lock plus current logical row | Snapshot visibility plus version chain |
| Write path | Exclusive lock plus direct logical update | Staged version plus commit publication |
| Blocking behavior | Readers may wait behind writers | Readers continue on older committed snapshot |
| Rollback | Undo/discard and release locks | Discard pending versions and release locks |
| Storage cost | Lower metadata overhead | Higher version-history overhead |

The crucial project invariant is that 2PL is still present and correct. MVCC is an additive mode, not a replacement for the baseline.

## Failure Scenarios

### Crash After WAL Append, Before Commit

- WAL contains `BEGIN` and data-change records but no `COMMIT`.
- Recovery ignores those changes.
- Result: no uncommitted state becomes visible after restart.

### Crash After Commit, Before Data Flush

- WAL already contains the committed logical change.
- Recovery replays committed state and rebuilds the current table image.
- Result: the committed transaction survives restart.

### Deadlock During 2PL Contention

- Two transactions wait on each other's locks.
- The waits-for graph cycle is detected.
- The youngest victim is aborted so the older transaction can complete.

### Rollback Of MVCC Delete Or Insert

- The transaction's staged versions are discarded.
- No foreign transaction ever sees those uncommitted versions.

## Design Trade-Offs

- Slotted heap pages are easy to reason about, but they are not optimized for compaction-heavy workloads.
- Logical recovery is smaller and easier to demo than ARIES, but less realistic for industrial storage engines.
- MVCC dramatically improves read concurrency, but it introduces version-history growth and future cleanup obligations.
- A lightweight optimizer keeps `EXPLAIN` understandable, but plan quality is limited by coarse statistics.

## Extension Points

- ARIES-style recovery with page LSNs and compensation records
- MVCC vacuum and garbage collection
- richer secondary-index maintenance for versioned rows
- histogram-based selectivity estimation
- additional physical operators such as hash join or index nested-loop join
- durability controls for flush frequency and sync semantics
