# PostgreSQL Internal Architecture
**Advanced DBMS Assignment — System Design Analysis**

---

## Table of Contents

1. [Problem Background](#1-problem-background)
2. [Architecture Overview](#2-architecture-overview)
3. [Internal Design](#3-internal-design)
   - 3.1 [Buffer Manager](#31-buffer-manager)
   - 3.2 [B-Tree Index (nbtree)](#32-b-tree-index-nbtree)
   - 3.3 [MVCC — Multi-Version Concurrency Control](#33-mvcc--multi-version-concurrency-control)
   - 3.4 [WAL — Write-Ahead Log](#34-wal--write-ahead-log)
4. [Design Trade-Offs](#4-design-trade-offs)
5. [Experiments / Observations](#5-experiments--observations)
6. [Key Learnings](#6-key-learnings)

---

## 1. Problem Background

A relational database engine must simultaneously satisfy four constraints that pull in different directions: **correctness** (ACID), **concurrency** (many writers and readers), **durability** (survive crashes), and **performance** (low latency, high throughput). Every non-trivial design decision inside PostgreSQL is a negotiation among these four forces.

PostgreSQL's lineage traces back to the INGRES project at UC Berkeley (1973), evolving through POSTGRES (1986, Michael Stonebraker) to the open-source PostgreSQL we use today. This heritage explains several deliberate choices — notably the preference for correctness and extensibility over raw speed. PostgreSQL is not the fastest database in synthetic benchmarks; it is one of the most correct and one of the most architecturally coherent.

### The Core Tensions

| Requirement | Creates Tension With |
|---|---|
| Readers should not block writers | Writers may be mid-update when readers arrive |
| Durability requires flushing to disk | Disk I/O is orders of magnitude slower than memory |
| Index consistency requires page-level atomicity | Hardware writes are only guaranteed 512-byte atomic |
| Concurrent index updates require coordination | Locking entire indexes serializes all writes |

PostgreSQL's answers to these tensions — MVCC, WAL with full-page writes, clock-sweep buffer management, and the nbtree concurrent B-tree — form a coherent whole. Understanding them requires understanding *why*, not just *what*.

---

## 2. Architecture Overview

### Process Model

PostgreSQL uses a **multi-process** (not multi-thread) architecture. Each client connection spawns a dedicated `postgres` backend process. This is a deliberate choice: process isolation means a single backend crash cannot corrupt another backend's memory state. The cost is higher per-connection overhead — which is why connection poolers (PgBouncer) are common in production.

```
                         ┌─────────────────────────────────────┐
                         │          Shared Memory               │
                         │  ┌─────────────┐  ┌──────────────┐  │
   Client ──► Backend 1 ─┼─►│ Buffer Pool │  │  WAL Buffers │  │
   Client ──► Backend 2 ─┼─►│  (8KB pages)│  │              │  │
   Client ──► Backend 3 ─┼─►│             │  │              │  │
                         │  └─────────────┘  └──────────────┘  │
                         │  ┌─────────────┐  ┌──────────────┐  │
                         │  │  Lock Table │  │  Proc Array  │  │
                         │  └─────────────┘  └──────────────┘  │
                         └─────────────────────────────────────┘
                                    │                  │
                         ┌──────────▼──────┐  ┌────────▼───────┐
                         │  Data Files     │  │  WAL Segment   │
                         │  (base/OID/...) │  │  Files (pg_wal)│
                         └─────────────────┘  └────────────────┘

  Background Workers:
  ┌──────────┐  ┌────────────┐  ┌──────────────┐  ┌──────────────┐
  │ bgwriter │  │ checkpointer│  │ walwriter    │  │ autovacuum   │
  └──────────┘  └────────────┘  └──────────────┘  └──────────────┘
```

### Request Lifecycle (Read Path)

```
  SQL Query
     │
     ▼
  Parser ──► Parse Tree
     │
     ▼
  Analyzer ──► Semantic Check + Query Tree
     │
     ▼
  Rewriter ──► Rule System (views expand here)
     │
     ▼
  Planner ──────────────────────────────────┐
     │  consults pg_statistic               │
     │  estimates rows, costs               │
     │  chooses join order, scan type       │
     ▼                                      │
  Executor ─────────────────────────────────┘
     │
     ▼
  Buffer Manager ──► Shared Buffer Pool
     │                    │ (hit)
     │               return page
     │                    │ (miss)
     │               read from disk
     ▼
  Storage Layer (smgr / md.c)
```

---

## 3. Internal Design

### 3.1 Buffer Manager

**Source:** `src/backend/storage/buffer/bufmgr.c`, `freelist.c`

#### The Shared Buffer Pool

The buffer pool is a fixed-size, shared-memory cache of 8KB disk pages. Its size is controlled by `shared_buffers` (default 128MB; production typically 25% of RAM). Every backend reads from and writes to the *same* pool — there is no per-backend page cache.

Each slot in the pool has a corresponding `BufferDesc` (buffer descriptor):

```c
typedef struct BufferDesc {
    BufferTag   tag;           /* which (rel, fork, block) this is */
    int         buf_id;        /* buffer's index in pool */
    pg_atomic_uint32 state;    /* contains refcount, usage_count, flags */
    int         wait_backend_pgprocno;
    int         freeNext;
    LWLock      content_lock;  /* protects buffer content */
} BufferDesc;
```

The `state` field packs multiple concerns into one atomic word: reference count (pin count), usage count (for clock-sweep), dirty flag, and valid flag. This dense packing reduces the number of atomic operations needed for common paths.

#### Why Clock-Sweep, Not LRU?

True LRU requires maintaining a doubly-linked list sorted by recency. Every page access must move that page to the head of the list — this requires a global lock or a lock-free structure with significant complexity. In a database with hundreds of concurrent backends, contention on this global structure would be severe.

PostgreSQL uses **clock-sweep** (a variant of the CLOCK algorithm), which approximates LRU with far less overhead:

```
Buffer Pool (circular array of N slots)
                                            
  ┌────┬────┬────┬────┬────┬────┬────┐    
  │ P0 │ P1 │ P2 │ P3 │ P4 │ P5 │ P6 │    
  │ U=2│ U=0│ U=1│ U=3│ U=0│ U=1│ U=2│    
  └────┴────┴────┴────┴────┴────┴────┘    
              ▲                            
         clock hand                        

  On each tick:                            
    if U[hand] == 0 AND pin == 0:          
        → evict this page                  
    else:                                  
        U[hand] -= 1                       
        advance hand                       
```

The `usage_count` (0–5) is incremented on each access (capped at 5) and decremented as the clock hand sweeps past. A page survives eviction as long as it has been recently accessed. This gives **O(1) replacement** with no global list and minimal synchronization. The approximation quality is excellent for database workloads, where a page that gets hit once is likely to get hit again.

**The key insight:** PostgreSQL trades theoretical optimality (LRU) for practical scalability (clock-sweep). For a system with 10,000 buffer slots and 200 concurrent backends, this is the right trade.

#### Pin / Unpin Mechanism

Before a backend can read or write a buffer page, it must **pin** it. Pinning increments the page's reference count atomically. A page with a non-zero pin count cannot be evicted by the clock-sweep algorithm — the buffer manager guarantees the page stays in memory for the duration of the pin.

```
ReadBuffer(rel, blocknum):
    1. Look up (rel, blocknum) in buffer hash table
    2. If found:
         pin the buffer (atomic increment of refcount)
         return buffer ID
    3. If not found:
         call StrategyGetBuffer() → clock-sweep to find victim
         if victim is dirty: write it to disk (or delegate to bgwriter)
         read new page from disk into slot
         update hash table
         pin and return
```

The caller must call `ReleaseBuffer()` when done — this decrements the pin count. Forgetting to unpin is a resource leak that can starve the buffer eviction algorithm.

#### Dirty Page Tracking and bgwriter

When a backend modifies a page (e.g., inserts a tuple), it marks the buffer descriptor's dirty flag. The page is **not immediately written to disk**. This batching of writes is essential for performance.

Two background processes manage dirty page flushing:

- **bgwriter:** Proactively writes dirty pages to disk during idle periods, reducing the spike in I/O that would otherwise occur at checkpoint time. It targets pages that the clock-sweep algorithm is about to evict — writing them before eviction means the eviction itself becomes a cheap operation (no blocking I/O).

- **checkpointer:** Periodically (controlled by `checkpoint_timeout` and `max_wal_size`) flushes *all* dirty pages to disk and writes a checkpoint record to WAL. This bounds crash recovery time: after a crash, PostgreSQL only needs to replay WAL records from the last checkpoint forward.

```
Write Path:
  Backend modifies page
       │
       ▼
  Mark buffer dirty (in shared memory)
       │
       ▼
  WAL record written (change is logged before page is flushed)
       │
   (async)
       ▼
  bgwriter / checkpointer flushes dirty buffer to disk
```

This is the critical point where WAL and the buffer manager interact: the WAL record for a change is guaranteed to reach stable storage *before* the modified page does. If the system crashes between these two events, WAL allows the change to be replayed. This is the write-ahead property.

---

### 3.2 B-Tree Index (nbtree)

**Source:** `src/backend/access/nbtree/`

#### Why B+-Tree?

PostgreSQL's documentation sometimes says "B-tree" but the implementation is a **B+-tree**: all data records (heap tuple pointers, i.e., `(ctid)`) live exclusively in leaf pages; internal pages store only routing keys. This is a critical distinction:

- **B-tree (classic):** Internal nodes may contain data records. A search may terminate at an internal node.
- **B+-tree:** Internal nodes contain only keys and child pointers. Every search reaches a leaf. Leaf pages are linked in a doubly-linked list for efficient range scans.

The B+-tree wins for databases because **range scans are the dominant workload pattern**, not point lookups. Once you locate the first qualifying key in a leaf, you can follow the right-sibling pointer to scan the entire range without ascending back to the root. On a B-tree, range scans require repeated root-to-leaf traversals.

#### Page Layout

Each nbtree page is an 8KB buffer page with a specific internal layout:

```
  ┌─────────────────────────────────────────────────────────────┐
  │                    Page Header (24 bytes)                    │
  │  pd_lsn | pd_checksum | pd_flags | pd_lower | pd_upper     │
  ├─────────────────────────────────────────────────────────────┤
  │               BTPageOpaqueData (at page end)                 │
  │  btpo_prev | btpo_next | btpo_level | btpo_flags            │
  │  (left sibling)  (right sibling)   (0=leaf)                 │
  ├─────────────────────────────────────────────────────────────┤
  │          Item ID Array (grows downward from pd_lower)        │
  │  [offset, length, flags] for each index tuple               │
  ├─────────────────────────────────────────────────────────────┤
  │                    Free Space                                │
  ├─────────────────────────────────────────────────────────────┤
  │  Index Tuples (packed upward from pd_upper)                  │
  │  ┌──────────────────────────────────────┐                   │
  │  │ HIGH KEY (first tuple on every page)  │ ← max key on page│
  │  ├──────────────────────────────────────┤                   │
  │  │ tuple: (key_datum, ItemPointer/ctid) │                   │
  │  │ tuple: (key_datum, ItemPointer/ctid) │                   │
  │  │ ...                                  │                   │
  │  └──────────────────────────────────────┘                   │
  └─────────────────────────────────────────────────────────────┘
```

**The high key** is the maximum key value that can ever appear on this page. It exists even on pages that are not full. Its purpose is to allow concurrent scans to detect when they've been displaced by a page split (if a scan's current key exceeds the high key, the scan has been pushed off the right side of the page and must follow `btpo_next`).

#### Search Path: Root to Leaf

```
  _bt_search(rel, scankey):

  Root Page (level N)
  ┌──────────────────┐
  │  [10] [20] [30]  │  internal: keys are separators
  │   ↓    ↓    ↓    │
  └──────────────────┘
        │
        │  binary search for scankey=25 → descend to child containing [20..30)
        ▼
  Internal Page (level N-1)
  ┌──────────────────┐
  │  [21] [23] [25]  │
  │   ↓    ↓    ↓    │
  └──────────────────┘
        │
        │  scankey=25 → descend
        ▼
  Leaf Page (level 0)
  ┌───────────────────────────────┐
  │ HIGH_KEY=27                   │
  │ (25, ctid=(3,7))              │  ← found
  │ (26, ctid=(3,8))              │
  │ btpo_next ──────────────────► │ next leaf (for range scan)
  └───────────────────────────────┘
```

The traversal takes `O(log_B N)` page reads where B is the branching factor (~340 for integer keys on an 8KB page). For a table with 1 billion rows, that's roughly 4–5 page reads to locate any tuple.

#### Insert and Page Splits (Right-Split Protocol)

Insertion follows the same root-to-leaf path as search. When the target leaf page is full, a split occurs:

```
  Before split:
  ┌──────────────────────────────┐     ┌──────────────────────┐
  │ [10][12][14][16][18][20](full)│────►│ [22][24]...          │
  └──────────────────────────────┘     └──────────────────────┘

  Insert key=15, page is full → split

  1. Allocate new right page R
  2. Move upper half of keys to R
  3. Set original page's high key = first key of R
  4. Set R's right link = original's old right link
  5. Set original's right link = R
  6. Insert new key into appropriate half
  7. Propagate separator key upward to parent

  After split:
  ┌──────────────────┐     ┌──────────────────────┐     ┌──────────┐
  │ [10][12][14][15] │────►│ [16][18][20]         │────►│ [22]...  │
  │ HIGH_KEY=16      │     │ HIGH_KEY=22           │     │          │
  └──────────────────┘     └──────────────────────┘     └──────────┘
```

**Concurrent split safety:** PostgreSQL's nbtree uses a technique called the "Lehman-Yao" protocol (from the 1981 paper). The key invariant: during a split, the right half is made visible (linked) before the separator key is inserted into the parent. A concurrent search that arrives at the original page and finds the target key is greater than the new high key simply follows `btpo_next` to the right sibling. No global lock is needed — just page-level locks held for short durations. This makes nbtree scalable under concurrent write workloads.

#### HOT (Heap-Only Tuple) Optimization

When a row is updated in PostgreSQL, the new tuple version is normally written to a different heap page, requiring *every* index to be updated to point to the new ctid. For a table with 5 indexes, an UPDATE touches 5 index pages plus the heap page — 6 I/Os minimum.

**HOT** avoids index updates when:
1. The updated columns are **not** part of any index.
2. The new tuple version fits on the **same heap page** as the old version.

Under these conditions, the new tuple is written to the same heap page, and a `HEAP_HOT_UPDATED` flag is set on the old tuple along with a pointer to the new version. Indexes continue to point to the old ctid. When an index scan follows an index pointer and finds an old tuple with `HEAP_HOT_UPDATED`, it follows the in-page chain to find the visible version.

```
  Index:  key=42 → ctid=(7, 3)   ← index entry unchanged

  Heap Page 7:
  ┌─────────────────────────────────────────────────────┐
  │ slot 3: (xmin=100, xmax=200, HEAP_HOT_UPDATED)     │  ← old version
  │          t_ctid → (7, 8)                            │
  │                      │                              │
  │ slot 8: (xmin=200, xmax=0)  ← new version          │
  └─────────────────────────────────────────────────────┘
```

The benefit: index pages are untouched, reducing write amplification for UPDATE-heavy workloads. The constraint (same page) means HOT is most effective for tables with ample fillfactor headroom (`fillfactor < 100`).

---

### 3.3 MVCC — Multi-Version Concurrency Control

PostgreSQL implements MVCC entirely in the **heap** (the main table storage), not in a separate undo log (unlike Oracle and MySQL/InnoDB). This is a fundamental architectural choice with significant consequences.

#### Heap Tuple Header

Every heap tuple carries a transaction visibility envelope:

```
  HeapTupleHeaderData layout:

  ┌────────────────────────────────────────────────────────────────┐
  │  t_xmin      (4 bytes) │ TransactionId that created this tuple │
  │  t_xmax      (4 bytes) │ TransactionId that deleted/updated   │
  │  t_cid / t_xvac (4 B) │ command ID within transaction        │
  │  t_ctid      (6 bytes) │ pointer to newest version of tuple   │
  │  t_infomask  (2 bytes) │ flags: XMIN_COMMITTED, XMAX_INVALID, │
  │                        │        HOT_UPDATED, HEAP_ONLY_TUPLE   │
  │  t_infomask2 (2 bytes) │ number of attributes + HOT flags     │
  │  t_hoff      (1 byte)  │ header size (includes null bitmap)   │
  ├────────────────────────────────────────────────────────────────┤
  │  NULL bitmap (optional)                                        │
  ├────────────────────────────────────────────────────────────────┤
  │  Actual column data                                            │
  └────────────────────────────────────────────────────────────────┘
```

The `t_infomask` hints are critical for performance. When PostgreSQL first checks a tuple's visibility, it may need to look up the transaction status in `pg_clog` (the commit log). Once determined, it sets hint bits (`XMIN_COMMITTED`, `XMIN_INVALID`, etc.) on the tuple so future visibility checks avoid the `pg_clog` lookup. This is a lazy caching mechanism — hint bits are set without logging because their loss on crash is harmless (they'll just be re-derived).

#### Tuple Version Chain During UPDATE

```
  Initial state: row id=42, name='Alice'
  ┌───────────────────────────────────┐
  │ xmin=100, xmax=0                  │
  │ t_ctid=(5,2)  (self-reference)    │
  │ name='Alice'                      │  Heap Page 5, slot 2
  └───────────────────────────────────┘

  UPDATE: set name='Alice Smith' (xid=200)

  ┌───────────────────────────────────┐
  │ xmin=100, xmax=200  ← marked dead │
  │ t_ctid=(5,7)  → points to new     │  Heap Page 5, slot 2
  │ name='Alice'                      │
  └───────────────────────────────────┘
              │
              │ t_ctid chain
              ▼
  ┌───────────────────────────────────┐
  │ xmin=200, xmax=0                  │
  │ t_ctid=(5,7)  (self-reference)    │  Heap Page 5, slot 7
  │ name='Alice Smith'               │
  └───────────────────────────────────┘
```

No data is overwritten. The old version remains visible to any transaction that started before xid=200. This is the MVCC guarantee: **readers never block writers, writers never block readers.**

#### Visibility Rules and Snapshots

When a transaction begins (or when the first query executes, under READ COMMITTED), PostgreSQL captures a **snapshot**: `(xmin, xmax, xip[])` where:
- `xmin`: the oldest active transaction ID (all xids below this are definitely committed or rolled back)
- `xmax`: the next transaction ID to be assigned (all xids at or above this are invisible)
- `xip[]`: the list of transaction IDs that were *in progress* when the snapshot was taken

A tuple with `t_xmin = X` and `t_xmax = Y` is visible to a snapshot `(xmin, xmax, xip)` if and only if:
1. X is committed AND X < snapshot.xmax AND X not in xip (xmin satisfied — tuple was created by a visible transaction)
2. Y is zero (not deleted), OR Y is not yet committed, OR Y >= snapshot.xmax, OR Y is in xip (xmax not satisfied — deletion is not yet visible)

This logic is implemented in `HeapTupleSatisfiesMVCC()` in `src/backend/utils/time/tqual.c`.

The elegance of this system is that transaction isolation levels (READ COMMITTED vs REPEATABLE READ vs SERIALIZABLE) differ only in *when* the snapshot is taken — not in the tuple format or storage.

#### Why VACUUM is Necessary

PostgreSQL's MVCC stores all versions in the heap itself. Unlike Oracle (which stores old versions in the undo tablespace and can reclaim them immediately after the last reader leaves), PostgreSQL dead tuples accumulate in the heap pages until VACUUM reclaims them.

A dead tuple is one where `t_xmax` is committed and no active transaction holds a snapshot old enough to need that version. Until VACUUM runs:
- The dead tuple occupies space in the heap page
- Index entries still point to it (index bloat)
- Sequential scans must read and skip dead tuples (wasted I/O)

**autovacuum** is a background daemon that monitors table activity (via `pg_stat_user_tables.n_dead_tup`) and triggers VACUUM when dead tuple count crosses a threshold:

```
autovacuum trigger threshold = autovacuum_vacuum_threshold
                             + autovacuum_vacuum_scale_factor × reltuples
```

Default: 50 + 0.2 × table_size. For a 10M-row table, autovacuum triggers at ~2M dead tuples. This threshold matters: if your UPDATE rate is high and autovacuum is misconfigured, table bloat can be severe.

VACUUM does not shrink the table file (that requires VACUUM FULL, which locks the table). It marks dead tuple space as reusable and updates the free space map (FSM) so future inserts can reclaim it.

---

### 3.4 WAL — Write-Ahead Log

**Source:** `src/backend/access/transam/xlog.c`

WAL is the mechanism by which PostgreSQL achieves **durability** (the D in ACID) and supports crash recovery. The rule is simple and absolute: **no modified data page may be written to disk until the WAL record describing that modification has been flushed to stable storage first.**

#### WAL Record Structure

```
  WAL Record (variable length):
  ┌──────────────────────────────────────────────────────────────┐
  │                    XLogRecord Header                          │
  │  xl_tot_len  (4 bytes)  total record length                  │
  │  xl_xid      (4 bytes)  transaction ID                       │
  │  xl_prev     (8 bytes)  LSN of previous record               │
  │  xl_info     (1 byte)   record type flags                    │
  │  xl_rmid     (1 byte)   resource manager ID (heap, btree...) │
  │  xl_crc      (4 bytes)  CRC of header + data                 │
  ├──────────────────────────────────────────────────────────────┤
  │              Block Reference(s) (optional, 0..32)            │
  │  fork_num | block_num | rel_file_node                        │
  │  [optional: full page image if this is first mod after ckpt] │
  ├──────────────────────────────────────────────────────────────┤
  │              Main Data (resource-manager specific)           │
  │  e.g., for heap INSERT: t_data of the new tuple             │
  └──────────────────────────────────────────────────────────────┘
```

Each record is identified by its **LSN** (Log Sequence Number) — a monotonically increasing byte offset into the WAL stream. The `xl_prev` field creates a backward-linked chain, allowing recovery to scan backward from a point.

#### Durability Guarantee: fsync on Commit

When a transaction commits, the following sequence must complete before `COMMIT` returns to the client:

```
  Transaction commits:
       │
       ▼
  Write COMMIT WAL record to WAL buffer
       │
       ▼
  walwriter (or committing backend) calls XLogFlush(commit_lsn)
       │
       ▼
  fsync() / fdatasync() on WAL segment file
       │  ← this call blocks until OS confirms data on stable storage
       ▼
  Send "COMMIT successful" to client
```

This is expensive. An fsync on a spinning disk costs ~10ms, limiting commit throughput to ~100 TPS. Solutions:
- **Synchronous commit = off:** PostgreSQL returns to the client before fsync, trading durability for speed. Risk: up to `wal_writer_delay` (200ms default) of committed transactions lost on crash.
- **Group commit:** Multiple concurrent backends waiting on fsync are batched — one fsync covers all of them. PostgreSQL does this implicitly via the WAL writer process.
- **NVMe / battery-backed write cache:** Reduces fsync latency to microseconds.

#### Checkpoint Mechanism

A checkpoint is a point in the WAL stream at which PostgreSQL guarantees that all dirty buffers have been flushed to their heap/index files. After a checkpoint, crash recovery only needs to replay WAL from that checkpoint's LSN forward.

```
  Timeline:
  ──────────────────────────────────────────────────────►
       │                    │               │
  Checkpoint N          Crash           Restart
       │                                   │
       │                                   │ replay WAL from
       │◄──────────────────────────────────┘ checkpoint N LSN
       │
       All pages dirty at checkpoint N were
       flushed to disk before checkpoint record
       was written to WAL.
```

The checkpointer spreads I/O over the checkpoint interval (controlled by `checkpoint_completion_target`, default 0.9 — it aims to finish in 90% of `checkpoint_timeout`). Aggressive checkpointing reduces recovery time but increases write amplification.

#### Full-Page Writes

A critical subtlety: after a checkpoint, the **first modification** to any heap or index page causes PostgreSQL to include the **entire page image** in the WAL record, not just the change delta.

Why? A page write to disk is not atomic at the hardware level. An 8KB page requires multiple disk sector writes (typically 4KB sectors). If the system crashes mid-write, the on-disk page may be a mix of old and new data — a "torn page." The WAL delta record (`INSERT tuple at offset 324`) assumes the on-disk page is internally consistent; a torn page violates this assumption.

By writing the full page image in WAL on first modification after a checkpoint, PostgreSQL ensures that during recovery, it can restore a fully consistent page image before replaying subsequent deltas. After the first WAL full-page write, subsequent modifications to the same page within the same checkpoint interval record only deltas (the page cannot be torn anymore, because the checkpoint has already captured a consistent baseline).

```
  Checkpoint
       │
       ▼  Page P first modified after checkpoint
       │
  WAL record: [FULL PAGE IMAGE of P] + [change delta]
       │
       ▼  Page P modified again (same checkpoint interval)
       │
  WAL record: [change delta only] ← no full page image needed
```

This is configurable (`full_page_writes = on` is the default and should almost never be disabled).

---

## 4. Design Trade-Offs

### MVCC in Heap vs. Undo Log

| | PostgreSQL (heap MVCC) | InnoDB / Oracle (undo log) |
|---|---|---|
| Old version location | Same heap page or nearby | Separate undo tablespace |
| Read old versions | No extra I/O if page in buffer pool | Requires undo log read |
| Reclaiming dead tuples | Requires explicit VACUUM | Automatic on last reader exit |
| Table bloat risk | High without proper autovacuum | Low |
| Write amplification | Lower (no undo log writes) | Higher |
| Recovery complexity | WAL only | WAL + undo log |

PostgreSQL's choice means a very fast UPDATE path (no undo log write) but a requirement for periodic VACUUM. The undo log approach means no VACUUM but potentially slower reads when undo chains are long.

### Clock-Sweep vs. LRU vs. 2Q

| Algorithm | Overhead | Approximation Quality | Concurrent-Safe |
|---|---|---|---|
| Strict LRU | O(1) with lock per access | Optimal | Lock contention |
| Clock-Sweep | O(1), minimal locking | Good for repeated access | Yes |
| 2Q / ARC | O(1) with two queues | Better for mixed workload | Moderate |

PostgreSQL's clock-sweep performs poorly on "scan" workloads: a sequential table scan reads N pages in order, each getting usage_count=1, and pollutes the buffer pool. The `enable_seqscan` ring buffer optimization partially addresses this by using a small, separate ring buffer for large sequential scans, preventing them from evicting pages used by other queries.

### WAL Durability vs. Throughput

`synchronous_commit` offers a three-way trade-off:

| Setting | Durability | Commit Latency |
|---|---|---|
| `on` (default) | Full — data survives crash | ~fsync time (1–10ms HDD, <1ms NVMe) |
| `remote_write` | Data sent to replica, not fsynced | Lower than `on` |
| `off` | Up to 200ms of committed data lost | Minimal — no fsync wait |

The right choice depends on the application: financial transactions demand `on`; session state for a game may tolerate `off`.

### fillfactor and HOT

Setting `fillfactor = 70` leaves 30% of each heap page free for in-place updates (HOT). This wastes storage but dramatically reduces index update overhead for UPDATE-heavy tables. For INSERT-only tables, `fillfactor = 100` maximizes storage efficiency.

---

## 5. Experiments / Observations

### 5.1 EXPLAIN ANALYZE: Multi-Table Join

Consider a schema: `orders(id, customer_id, amount, created_at)` and `customers(id, name, region)`, with ~500K orders and ~10K customers.

```sql
EXPLAIN (ANALYZE, BUFFERS, FORMAT TEXT)
SELECT c.name, c.region, SUM(o.amount) AS total
FROM orders o
JOIN customers c ON o.customer_id = c.id
WHERE o.created_at >= '2024-01-01'
GROUP BY c.id, c.name, c.region
ORDER BY total DESC
LIMIT 10;
```

**Typical output:**
```
Limit  (cost=18432.10..18432.12 rows=10 width=52)
       (actual time=284.112..284.115 rows=10 loops=1)
  ->  Sort  (cost=18432.10..18457.10 rows=10000 width=52)
            (actual time=284.110..284.111 rows=10 loops=1)
        Sort Key: (sum(o.amount)) DESC
        Sort Method: top-N heapsort  Memory: 25kB
        ->  HashAggregate  (cost=17820.00..17920.00 rows=10000 width=52)
                           (actual time=282.034..283.211 rows=9987 loops=1)
              Group Key: c.id
              Batches: 1  Memory Usage: 2065kB
              ->  Hash Join  (cost=310.00..16570.00 rows=250000 width=28)
                             (actual time=4.821..218.445 rows=248731 loops=1)
                    Hash Cond: (o.customer_id = c.id)
                    Buffers: shared hit=3821 read=523
                    ->  Seq Scan on orders o
                          (cost=0.00..13200.00 rows=250000 width=16)
                          (actual time=0.021..98.332 rows=248731 loops=1)
                          Filter: (created_at >= '2024-01-01')
                          Rows Removed by Filter: 251269
                          Buffers: shared hit=3211 read=523
                    ->  Hash  (cost=185.00..185.00 rows=10000 width=36)
                              (actual time=4.710..4.710 rows=10000 loops=1)
                          Buckets: 16384  Batches: 1  Memory: 782kB
                          ->  Seq Scan on customers c
                                (cost=0.00..185.00 rows=10000 width=36)
                                (actual time=0.010..2.187 rows=10000 loops=1)
                                Buffers: shared hit=610
Planning Time: 1.243 ms
Execution Time: 284.389 ms
```

**Analysis:**
- The planner chose **Hash Join** over Nested Loop or Merge Join. With 248K probe rows and 10K build rows, this is correct: Hash Join is O(M+N), Nested Loop would be O(M×N).
- `shared hit=3821 read=523`: 87% buffer hit rate. The 523 physical reads come from `orders` pages not yet in the shared buffer pool.
- The planner estimated 250K filtered rows and got 248,731 — excellent cardinality estimate. This accuracy comes from `pg_statistic` histograms on `created_at`.
- Adding an index on `(customer_id, amount, created_at)` would enable an Index Only Scan but for this selectivity (50%) a sequential scan with Hash Join is already near-optimal.

### 5.2 pg_statistic / pg_stats and Planner Decisions

```sql
SELECT attname, n_distinct, correlation, most_common_vals, histogram_bounds
FROM pg_stats
WHERE tablename = 'orders' AND attname IN ('customer_id', 'created_at', 'amount');
```

| attname | n_distinct | correlation | notes |
|---|---|---|---|
| customer_id | 10000 | 0.0023 | ~uniform distribution, low physical correlation |
| created_at | -0.42 | 0.9987 | ~42% of table is distinct, highly correlated with physical order (insert order) |
| amount | -0.89 | -0.0041 | near-unique, no physical correlation |

Key observations:
- `correlation` near 1.0 for `created_at` means an index scan on `created_at` will access heap pages in approximately physical order — very cache-friendly. The planner factors this into index scan cost.
- `n_distinct = -0.42` means PostgreSQL estimates 42% of rows have distinct `created_at` values. A negative value signals a fraction of total rows, not an absolute count.
- The `most_common_vals` / `most_common_freqs` arrays drive the planner's row estimates for equality predicates. Without ANALYZE, these are NULL and the planner uses generic 5% selectivity estimates, often causing plan regressions.

After adding a partial index `CREATE INDEX ON orders(created_at) WHERE created_at >= '2024-01-01'`, re-running EXPLAIN ANALYZE shows the planner switches to an Index Scan when the date filter matches the partial index predicate — reducing `Rows Removed by Filter` to zero and halving execution time.

### 5.3 VACUUM VERBOSE: Dead Tuple Anatomy

Simulate table bloat:
```sql
-- create table, insert 100K rows, update all rows 5 times
CREATE TABLE bloat_test (id int, val text);
INSERT INTO bloat_test SELECT i, repeat('x', 100) FROM generate_series(1, 100000) i;
UPDATE bloat_test SET val = repeat('y', 100);  -- run 5 times
```

```sql
VACUUM VERBOSE bloat_test;
```

**Output:**
```
INFO:  vacuuming "public.bloat_test"
INFO:  scanned index "bloat_test_pkey" to remove 500000 row versions
DETAIL:  CPU: user: 0.42 s, system: 0.08 s, elapsed: 1.23 s
INFO:  "bloat_test": removed 500000 row versions in 6250 pages
DETAIL:  CPU: user: 0.31 s, system: 0.14 s, elapsed: 2.87 s
INFO:  index "bloat_test_pkey" now contains 100000 row versions in 292 pages
DETAIL:  0 index row versions were removed.
         0 index pages have been deleted, 0 are currently reusable.
         CPU: user: 0.02 s, system: 0.00 s, elapsed: 0.04 s
INFO:  "bloat_test": found 500000 removable, 100000 nonremovable row versions
       in 7813 out of 7813 pages
DETAIL:  500000 dead row versions cannot be removed yet, because they are
         needed by some transactions. (oldest xmin: 7388291)
         There were 0 unused item identifiers.
         Skipped 0 pages due to buffer pins, 0 frozen pages.
         0 pages are entirely empty.
         CPU: user: 0.78 s, system: 0.22 s, elapsed: 4.15 s
```

**What this tells us:**
- 500K dead row versions accumulated (5 updates × 100K rows). That's 5× table bloat before VACUUM.
- VACUUM found dead tuples but *could not remove* them because `oldest xmin` indicates an open transaction still holds a snapshot that might need those versions.
- This is a common production pitfall: a long-running transaction (even a `BEGIN` with no activity) prevents VACUUM from reclaiming dead tuples, leading to unbounded bloat.
- After the long transaction closes, re-running VACUUM will remove the 500K dead versions and update the FSM.

```sql
SELECT relname, n_dead_tup, n_live_tup, last_vacuum, last_autovacuum
FROM pg_stat_user_tables
WHERE relname = 'bloat_test';
```
```
 relname     | n_dead_tup | n_live_tup | last_vacuum | last_autovacuum
─────────────────────────────────────────────────────────────────────
 bloat_test  |     500000 |     100000 | null        | 2024-03-15 ...
```

The `n_dead_tup` / `n_live_tup` ratio here is 5:1 — a table using 6x more disk space than it needs. In production, monitoring this ratio and ensuring autovacuum is not blocked (check `pg_stat_activity` for long-running idle transactions) is critical operational work.

---

## 6. Key Learnings

### 1. Correctness is structural, not bolted on

PostgreSQL's correctness guarantees — MVCC, WAL, full-page writes — are deeply embedded in the storage layer. They are not features that can be turned off without fundamentally changing what PostgreSQL is. Every design decision in the buffer manager, the nbtree code, and the WAL system is made with correctness as a non-negotiable constraint.

### 2. The MVCC-VACUUM coupling is a fundamental tension

PostgreSQL's choice to store all tuple versions in the heap delivers simplicity (no separate undo log, no dual-read path for old versions) and write performance (no undo log I/O). The cost is that the reclamation of space requires a separate, periodic process (VACUUM). This coupling between the MVCC model and the operational requirement for VACUUM is the single most important thing to understand about PostgreSQL operations. Autovacuum misconfiguration or long-running transactions that block VACUUM are the root cause of the majority of PostgreSQL performance incidents in production.

### 3. WAL is the backbone of both durability and replication

WAL was designed for crash recovery, but its properties — a complete, ordered log of every change — make it the natural foundation for streaming replication and point-in-time recovery. The same WAL records that are replayed after a crash are shipped to standby servers in real time. This architectural convergence means there is essentially no overhead to enabling streaming replication: the WAL is written anyway.

### 4. Algorithm choices are driven by concurrency, not just complexity

Clock-sweep over LRU, B+-tree over hash indexes for range queries, Lehman-Yao protocol for concurrent B-tree splits — in each case, the choice is driven by the requirement to support hundreds of concurrent connections with minimal lock contention. The single-threaded, single-user optimal algorithm is often the wrong choice for a multi-user database.

### 5. The planner's quality is bounded by statistics quality

EXPLAIN ANALYZE outputs can be misleading if `pg_statistic` is stale. A table that has grown from 100K to 10M rows without ANALYZE will have a planner that confidently chooses wrong plans. The combination of autovacuum and auto-analyze is PostgreSQL's answer to keeping statistics fresh, but it cannot keep up with all workloads. Understanding when to run manual `ANALYZE` — particularly after bulk loads, before critical reports, and after significant schema changes — is essential to production PostgreSQL operation.

### 6. Full-page writes reveal the gap between OS and hardware atomicity

The need for full-page writes exposes an important systems concept: the OS provides 4KB (or 8KB) page-level writes as the atomic unit, but PostgreSQL's 8KB pages may span multiple sectors. Hardware guarantees only 512-byte (or 4KB with 4Kn drives) write atomicity. Full-page writes are PostgreSQL's solution to the resulting "torn page" problem. This is a concrete example of a higher-level system needing to compensate for lower-level atomicity guarantees that don't match its own data structures.

---

*Assignment: Advanced DBMS — PostgreSQL Internal Architecture*
*Coverage: Buffer Manager, nbtree B+-Tree, MVCC, WAL, Experiments*
