# MySQL / InnoDB Storage Engine
### Advanced DBMS — System Design Deep Dive

---

## Table of Contents

1. [Problem Background](#1-problem-background)
2. [Architecture Overview](#2-architecture-overview)
3. [Internal Design](#3-internal-design)
4. [Design Trade-Offs](#4-design-trade-offs)
5. [Experiments / Observations](#5-experiments--observations)
6. [Key Learnings](#6-key-learnings)

---

## 1. Problem Background

### The Fundamental Challenge

A storage engine sits between SQL semantics and raw disk I/O. Its job is deceptively hard: it must deliver durability (writes that survive crashes), isolation (concurrent transactions that don't corrupt each other), and performance (fast reads and writes) — simultaneously and cheaply. These three properties are in direct tension.

InnoDB became MySQL's default storage engine in MySQL 5.5 (2010) and has remained so because it solved this three-way tension better than MyISAM (its predecessor), which offered no transactions, no foreign keys, and only table-level locking. MyISAM was fast for pure reads, but any real application with concurrent writes would see contention grind it to a halt.

### What InnoDB Had to Get Right

The core problems InnoDB's designers faced:

**Crash Recovery**: If the server dies mid-write, how do you ensure the database wakes up in a consistent state? Writing data directly to disk in-place is dangerous — a partial write leaves you with corruption. You need a way to make writes atomic from the perspective of a crash.

**Concurrent Readers and Writers**: If a writer is modifying row R while a reader is scanning it, the reader must see a consistent snapshot — but blocking the reader on every write would be unacceptably slow. You need a versioning mechanism.

**Range Query Performance**: Disk is 1000x slower than RAM. Queries like `SELECT * FROM orders WHERE user_id BETWEEN 100 AND 200` must touch as few disk pages as possible. If related rows are physically scattered, every row access is a random I/O — catastrophic for large tables.

**Row-Level Isolation Without Phantom Reads**: SERIALIZABLE and REPEATABLE READ semantics require that a transaction not see new rows inserted by others in a range it already scanned. Preventing phantoms without full table locks requires careful locking design.

InnoDB's answers to these problems — clustered indexes, MVCC via undo logs, redo-log-based WAL, and gap locking — are not independent features bolted together. They form an interconnected system where each design decision constrains and enables the others.

---

## 2. Architecture Overview

### High-Level Component Map

```
┌─────────────────────────────────────────────────────────────────┐
│                        MySQL Server Layer                        │
│         (SQL Parser → Optimizer → Execution Engine)              │
└────────────────────────────┬────────────────────────────────────┘
                             │  Storage Engine API (handler interface)
┌────────────────────────────▼────────────────────────────────────┐
│                      InnoDB Storage Engine                        │
│                                                                   │
│  ┌─────────────────────────────────────────────────────────┐     │
│  │                     Buffer Pool                          │     │
│  │   ┌──────────┐  ┌──────────┐  ┌──────────┐             │     │
│  │   │ Instance │  │ Instance │  │ Instance │  ...         │     │
│  │   │    0     │  │    1     │  │    2     │             │     │
│  │   └──────────┘  └──────────┘  └──────────┘             │     │
│  │   Each instance: LRU list, free list, flush list         │     │
│  │   Adaptive Hash Index (AHI) sits on top of buffer pool  │     │
│  └─────────────────────────────────────────────────────────┘     │
│                                                                   │
│  ┌──────────────────┐     ┌───────────────────────────────┐      │
│  │  Undo Tablespace │     │        Redo Log               │      │
│  │  (undo_001,      │     │   (ib_logfile0, ib_logfile1)  │      │
│  │   undo_002, ...) │     │   Circular buffer on disk     │      │
│  └──────────────────┘     └───────────────────────────────┘      │
│                                                                   │
│  ┌─────────────────────────────────────────────────────────┐     │
│  │              Data + Index Tablespaces                    │     │
│  │         (ibd files: one per table or shared)             │     │
│  │   B+ trees on 16KB pages — clustered + secondary        │     │
│  └─────────────────────────────────────────────────────────┘     │
│                                                                   │
│  ┌──────────────────┐     ┌───────────────────────────────┐      │
│  │  Lock Manager    │     │   Purge Thread                │      │
│  │  (row, gap, next │     │   (reclaims undo logs no      │      │
│  │   key locks)     │     │    longer needed by any tx)   │      │
│  └──────────────────┘     └───────────────────────────────┘      │
└─────────────────────────────────────────────────────────────────┘
                             │
              ┌──────────────▼──────────────┐
              │         Raw Disk / SSD       │
              │  .ibd files, ib_logfile*,    │
              │  undo tablespace files       │
              └─────────────────────────────┘
```

### The Write Path (End-to-End)

When a transaction executes `UPDATE orders SET status='shipped' WHERE id=42`:

```
  Transaction commits UPDATE
         │
         ▼
  1. Acquire row lock on id=42
         │
         ▼
  2. Write UNDO record (old version of row → undo log)
         │
         ▼
  3. Modify page in buffer pool (in-place update)
         │
         ▼
  4. Write REDO log record (what changed, new value)
         │
         ▼
  5. On COMMIT: flush redo log to disk (fsync)
         │
         ▼
  6. Page eventually flushed to .ibd file by background thread
         │
         ▼
  7. Purge thread eventually reclaims undo record
         (once no active transaction needs it for MVCC)
```

Steps 2 and 4 together are the heart of InnoDB's durability story: undo log enables rollback and MVCC reads; redo log enables crash recovery. The actual data page write (step 6) is intentionally deferred and can be batched.

---

## 3. Internal Design

### 3.1 Clustered Indexes: Data Is the Index

#### The Core Idea

In most database systems, there is a conceptual separation between an index (a sorted data structure pointing to rows) and a heap (an unordered pile of rows). You look up the index to get a pointer (a heap tuple ID or row offset), then follow that pointer to get the actual row. PostgreSQL works this way.

InnoDB takes a different approach: the primary key B+ tree *is* the table. The leaf pages of the primary key index store the complete row data, not pointers to row data. This is called a **clustered index**.

```
PRIMARY KEY B+ TREE (clustered)

                    ┌──────────────────┐
                    │  Internal Node   │
                    │  [42 | 100 | 200]│
                    └────┬──────┬──────┘
                         │      │
           ┌─────────────┘      └──────────────┐
           ▼                                    ▼
  ┌─────────────────────┐            ┌─────────────────────┐
  │    Leaf Page        │◄──────────►│    Leaf Page        │
  │  ┌───┬─────────────┐│            │  ┌────┬────────────┐│
  │  │ 1 │ name=Alice  ││            │  │ 42 │ name=Bob   ││
  │  │   │ age=30      ││            │  │    │ age=25     ││
  │  │   │ city=NYC    ││            │  │    │ city=LA    ││
  │  ├───┼─────────────┤│            │  ├────┼────────────┤│
  │  │ 5 │ name=Carol  ││            │  │ 50 │ name=Dave  ││
  │  │   │ age=28      ││            │  │    │ age=33     ││
  │  │   │ city=Chicago││            │  │    │ city=Boston││
  │  └───┴─────────────┘│            │  └────┴────────────┘│
  │  [prev_page_ptr]     │            │  [next_page_ptr]    │
  └─────────────────────┘            └─────────────────────┘
         ▲ doubly-linked list of leaf pages
```

The leaf pages are doubly-linked, which is crucial: a range scan on `id BETWEEN 1 AND 50` traverses from the first matching leaf page to the last using the sibling pointers — no random jumps required.

#### Why Clustered Index Wins for Range Scans

Consider `SELECT * FROM orders WHERE id BETWEEN 1000 AND 2000`. With a heap-based approach, rows 1000–2000 could be scattered across hundreds of random disk pages — each a separate I/O. With a clustered index, these rows are physically adjacent on disk (modulo page splits), and a sequential scan of a few leaf pages retrieves them all. The I/O pattern changes from random to sequential, which is orders of magnitude faster on spinning disks and still significantly better on SSDs.

For primary key lookups (`WHERE id = 42`), the B+ tree traversal directly lands you on the page containing the full row. No secondary lookup is needed.

#### Secondary Indexes: The Double Lookup Cost

Secondary indexes are *separate* B+ trees. Their leaf pages do not store row data — they store the primary key value.

```
SECONDARY INDEX on (city)

  Leaf Page:
  ┌──────────────────────────────┐
  │ city=Boston  │ PK=50         │
  │ city=Chicago │ PK=5          │
  │ city=LA      │ PK=42         │
  │ city=NYC     │ PK=1          │
  └──────────────────────────────┘
         │
         │ (lookup PK in clustered index)
         ▼
  Clustered index B+ tree → fetch full row
```

Why store PK rather than a physical row pointer? Because rows in a clustered index can move (page splits cause row migration). If secondary indexes stored physical pointers, every page split would require updating every secondary index that pointed to any moved row — an O(n * #secondary_indexes) operation on every insert. By storing the logical primary key, secondary indexes remain valid regardless of physical relocation; only the clustered index itself needs updating.

The cost: a query using a secondary index incurs **two B+ tree traversals** — one on the secondary index to get the PK, then one on the clustered index to fetch the row. This is called a "double lookup" or "bookmark lookup." The optimizer knows this cost and may choose a full clustered index scan if the selectivity isn't high enough.

**Covering indexes** (where the secondary index contains all columns the query needs) eliminate the second lookup entirely — InnoDB can answer from the secondary index leaf page without touching the clustered index.

#### No Explicit Primary Key: Hidden Row ID

If you define a table without a `PRIMARY KEY`, InnoDB silently adds a hidden 6-byte `rowid` column and uses it as the clustered index key. This rowid is a globally monotonically incrementing integer maintained by a system counter. Rows are still clustered — you just can't use the clustered index efficiently because no application column maps to it. Range queries on your actual columns will hit a secondary index (if one exists) and pay the double lookup cost, or do a full scan. **Always define an explicit primary key.**

---

### 3.2 Buffer Pool: Caching and the Problem of Scan Pollution

#### Structure

The buffer pool is InnoDB's main memory cache. It holds 16KB pages read from disk. The entire data access path goes through it: reads look here first; writes modify pages here; background threads flush dirty pages back to disk.

For large deployments, InnoDB partitions the buffer pool into multiple **instances** (controlled by `innodb_buffer_pool_instances`, default 8 when pool ≥ 1GB). Each instance has its own mutex, LRU list, and page hash. This reduces contention on the buffer pool mutex — a major bottleneck in earlier versions.

Each instance manages pages in three lists:
- **Free list**: pages not currently in use
- **LRU list**: pages in use, ordered by recency
- **Flush list**: dirty pages that need to be written to disk, ordered by oldest modification (LSN)

#### The Young/Old LRU Sublist Problem

A naive LRU would evict the least-recently-used page when space is needed. This works well under normal access patterns, but fails catastrophically during a full-table scan. A `SELECT * FROM large_table` (e.g., for a report or ANALYZE) reads every page of the table exactly once and then never touches them again. With a naive LRU, this scan would evict the entire working set — all the hot pages for normal OLTP queries — replacing them with pages that will never be accessed again. After the scan, every subsequent OLTP query would be a cache miss.

InnoDB's solution: a **two-zone LRU**:

```
Buffer Pool LRU List (conceptual)
                                        3/8 of list    5/8 of list
                                        ┌──────────────┬──────────────────────────┐
  Most Recent                           │  "Young"     │        "Old"             │  Least Recent
  (Head)  ◄────────────────────────────┤  sublist     │        sublist           ├──────► (Tail/Eviction)
                                        │  (hot pages) │   (new pages go here)   │
                                        └──────────────┴──────────────────────────┘
                                                        ▲
                                              midpoint insertion point
```

When a page is first read from disk (e.g., by a scan), it is inserted at the **midpoint** — the head of the old sublist, not the head of the entire LRU. If it is accessed again within `innodb_old_blocks_time` milliseconds (default 1000ms), it is promoted to the young sublist. Pages that are only read once (as in a scan) stay in the old sublist and are evicted without disturbing the young sublist.

This means a full-table scan that reads millions of pages will mostly fill and recycle the old sublist, leaving hot OLTP pages in the young sublist untouched. This is a targeted fix for a specific pathology and it works well in practice.

#### Adaptive Hash Index (AHI)

InnoDB monitors access patterns to the B+ tree. If it detects that the same set of pages is being accessed repeatedly via the same key pattern (e.g., a specific column value appearing in thousands of queries), it builds an **in-memory hash index** automatically on top of those B+ tree pages.

A B+ tree lookup for `WHERE id = 42` requires traversing from root to leaf: typically 3–4 I/Os for a large table (though usually cached). A hash lookup is O(1). For very hot, point-lookup-heavy workloads, AHI can reduce lookup latency significantly.

AHI is a pure optimization: it's automatically built and dropped, never persisted to disk, and can be disabled with `innodb_adaptive_hash_index=OFF`. However, it adds overhead for write-heavy workloads (every write must invalidate the corresponding AHI entry) and can cause contention on the AHI latch. On workloads with many concurrent writers and high key diversity, disabling AHI sometimes improves throughput.

---

### 3.3 Undo Logs: The Foundation of MVCC and Rollback

#### What Undo Logs Are

When InnoDB updates a row, it does not write the new value to a separate location and atomically swap pointers (as PostgreSQL does). It modifies the row **in place** in the buffer pool page. But before doing so, it writes the **old version** of the row data to an undo log record.

This undo record serves two purposes:
1. **Rollback**: if the transaction is rolled back, InnoDB can reverse the in-place modification by reapplying the undo record.
2. **MVCC read consistency**: other transactions that need to see an older version of the row can reconstruct it by following the undo chain.

#### Undo Log Chain Structure

Each row in InnoDB has two hidden system columns: `DB_TRX_ID` (the ID of the last transaction that modified this row) and `DB_ROLL_PTR` (a pointer to the most recent undo log record for this row).

```
Current row in clustered index page:
┌─────────────────────────────────────────────────────┐
│ id=42 | name="Bob" | status="shipped"               │
│ DB_TRX_ID=1005 | DB_ROLL_PTR ──────────────────────┼──┐
└─────────────────────────────────────────────────────┘  │
                                                          │
           ┌──────────────────────────────────────────────┘
           ▼
  Undo Log Record (trx 1005's update):
  ┌─────────────────────────────────────────────────────┐
  │ TRX_ID=1005 | old status="processing"              │
  │ ROLL_PTR ──────────────────────────────────────────┼──┐
  └─────────────────────────────────────────────────────┘  │
                                                           │
           ┌─────────────────────────────────────────────┘
           ▼
  Undo Log Record (trx 998's update):
  ┌─────────────────────────────────────────────────────┐
  │ TRX_ID=998  | old status="pending"                 │
  │ ROLL_PTR ──────────────────────────────────────────┼──┐
  └─────────────────────────────────────────────────────┘  │
                                                           │
           ┌─────────────────────────────────────────────┘
           ▼
  Undo Log Record (trx 901's insert):
  ┌─────────────────────────────────────────────────────┐
  │ TRX_ID=901  | (original INSERT — row didn't exist) │
  │ ROLL_PTR = NULL                                     │
  └─────────────────────────────────────────────────────┘
```

A transaction that needs to read the row as of an earlier point in time traverses this chain, applying each undo record in reverse, until it reaches the version that was current at the desired snapshot point.

#### MVCC: How InnoDB Implements Consistent Reads

When a transaction starts (or when a statement executes, depending on isolation level), InnoDB creates a **read view**. A read view records:
- The current transaction's ID
- The IDs of all currently active transactions (transactions that have started but not yet committed)
- The smallest active transaction ID
- The next transaction ID to be assigned

To decide whether a row version is visible to a read view, InnoDB applies this logic:
- If `DB_TRX_ID < min_active_trx_id`: the row was committed before this read view was created → **visible**
- If `DB_TRX_ID >= next_trx_id`: the row was created after this read view → **not visible**, follow undo chain
- If `DB_TRX_ID` is in the active transaction list: the row was modified by a transaction that hadn't committed when this view was taken → **not visible**, follow undo chain
- If `DB_TRX_ID` is our own transaction: **visible** (we can see our own writes)

InnoDB traverses the undo chain until it finds a version that satisfies these conditions. Under REPEATABLE READ, the read view is created at the start of the first consistent read in the transaction and reused for all subsequent reads. Under READ COMMITTED, a fresh read view is created per statement.

#### InnoDB vs PostgreSQL MVCC: A Structural Comparison

This is the deepest architectural divergence between the two systems:

| Dimension | InnoDB | PostgreSQL |
|---|---|---|
| Where old versions live | Separate undo log files | Same heap table (older tuple versions stay in-place) |
| How row is modified | Modified in-place; old value written to undo | New tuple appended; old tuple marked with `xmax` |
| Cleanup mechanism | Purge thread (background, asynchronous) | VACUUM (explicit, or autovacuum) |
| Read path | Follow undo chain backward | Scan heap; filter by `xmin`/`xmax` visibility |
| Write amplification | One write per column changed (undo diff) | Full tuple written for every update |
| Table bloat | Undo tablespace can grow; separate from table | Table file itself grows (dead tuples in heap) |

**Implication for write-heavy workloads**: PostgreSQL's approach generates dead tuples inside the table file. Heavy write workloads create enormous table bloat, and VACUUM must work constantly to reclaim space. If VACUUM falls behind (e.g., a long-running transaction holds an old snapshot), the table file balloons. InnoDB's undo log grows instead — it's externalized from the data file — and the purge thread reclaims undo records as soon as no active transaction needs them.

**Implication for read-heavy workloads with long-lived transactions**: InnoDB's undo chain can grow very long if a transaction holds a read view for a long time while other transactions keep updating the same row. Reading that row requires traversing a potentially long chain. PostgreSQL would need VACUUM to have cleaned up dead tuples, but the read itself doesn't traverse a chain — it looks at the heap versions in-place.

#### Undo Tablespace Management

Undo logs are stored in dedicated undo tablespace files (`undo_001`, `undo_002`, etc.) since MySQL 8.0. They can be monitored and truncated online. Long-running transactions are the primary cause of undo space growth — the purge thread cannot reclaim undo records that might still be needed by active read views.

---

### 3.4 Redo Logs: Crash Recovery Without Sacrificing Performance

#### The Durability Problem

InnoDB updates pages in the buffer pool (RAM). Flushing the modified data page to disk on every write would be correct but brutally slow — random I/O to update an arbitrary page on disk, possibly with a seek, on every transaction commit. This is unacceptable.

The key insight: **you don't need to flush the data page; you only need to flush a description of the change**. This description is small, and can be written sequentially to a dedicated log file. That's the redo log.

#### Redo Log Structure: A Circular Buffer

```
Redo Log Files (circular, sequential writes)

  ┌─────────────────────────────────────────────────────────────┐
  │               ib_logfile0                                   │
  │  ┌──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┐ │
  │  │ LSN  │ LSN  │ LSN  │ LSN  │ LSN  │ LSN  │ LSN  │ LSN  │ │
  │  │ 1000 │ 1001 │ 1002 │ 1003 │ 1004 │ 1005 │ 1006 │ 1007 │ │
  │  └──────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┘ │
  └─────────────────────────────────────────────────────────────┘
  ┌─────────────────────────────────────────────────────────────┐
  │               ib_logfile1                                   │
  │  ┌──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┐ │
  │  │ LSN  │ LSN  │ LSN  │ LSN  │ LSN  │ LSN  │ LSN  │      │ │
  │  │ 1008 │ 1009 │ 1010 │ 1011 │ 1012 │ 1013 │ 1014 │ ◄──  │ │
  │  └──────┴──────┴──────┴──────┴──────┴──────┴──────┘  │   │ │
  └────────────────────────────────────────────────────────┼───┘
                                                           │
                                                      write pointer
   (wraps around — but only overwrites LSNs already
    safely written to data pages, tracked via checkpoint)

  Checkpoint LSN: oldest LSN for which data page is NOT yet
                  flushed to disk. Log space before this is reclaimable.
```

The redo log files are fixed-size and used as a circular buffer. The **Log Sequence Number (LSN)** is a monotonically increasing byte offset into the logical redo log stream. Every redo log record is tagged with an LSN.

**Checkpoint**: InnoDB periodically records a checkpoint LSN — the LSN up to which all dirty pages have been flushed to disk. Log space before the checkpoint LSN can be reused. If the write pointer catches up to the checkpoint (log is "full"), InnoDB must stall foreground writes to let the checkpoint advance — a known bottleneck for very write-heavy workloads with a small redo log.

In MySQL 8.0.30+, the redo log infrastructure was redesigned to allow dynamic resizing and a single logical log file (`#ib_redo*` files), removing the two-file limitation.

#### Crash Recovery

On restart after a crash:
1. InnoDB reads the last checkpoint LSN from the redo log header
2. It replays all redo log records from the checkpoint LSN to the end of the log (redo phase)
3. It then applies undo logs to roll back any transactions that were in-flight at crash time (undo phase)

This is the classic ARIES-style recovery: redo everything, then undo uncommitted transactions.

#### InnoDB vs PostgreSQL: WAL Philosophy

Both InnoDB and PostgreSQL implement Write-Ahead Logging (WAL), but the mechanics differ in a subtle and important way:

- **InnoDB**: modifies the data page **in-place** in the buffer pool and writes a compact **redo record** describing the change (e.g., "at page 3245, offset 120, change 8 bytes from X to Y"). The redo record is small because it only needs to describe the delta.

- **PostgreSQL WAL**: appends a new heap tuple to the table and writes a WAL record describing the new tuple insertion. The old tuple stays in the heap with its `xmax` set. PostgreSQL WAL records are often larger because they sometimes include the full page image (for `full_page_writes=on`, required after a checkpoint).

The in-place update approach means InnoDB's redo log grows linearly with the number of bytes changed, not with the number of rows touched (excluding full-page images). PostgreSQL's WAL under `full_page_writes` includes full 8KB page images for the first modification to a page after each checkpoint, which significantly increases WAL volume.

---

### 3.5 Locking: Gap Locks and Phantom Prevention

#### Row-Level Locking

InnoDB implements row-level locking by locking index records, not actual rows. A `SELECT ... FOR UPDATE` or an `UPDATE` acquires an **exclusive lock** on the index record (in the clustered index). Concurrent readers using MVCC do not take locks at all (for consistent reads) — they read the appropriate version from the undo chain.

This is fundamentally different from PostgreSQL's approach: PostgreSQL uses a purely optimistic MVCC model at READ COMMITTED and REPEATABLE READ — no row locks are taken for ordinary reads. For true serializable isolation, PostgreSQL uses **Serializable Snapshot Isolation (SSI)**, which tracks read-write dependencies and aborts transactions that would form anomalous cycles. There are no equivalent gap locks in PostgreSQL.

#### The Phantom Problem and Gap Locks

A **phantom read** occurs when a transaction executes the same range query twice and gets different sets of rows because another transaction inserted a matching row in between:

```
Transaction A (REPEATABLE READ):
  T1: SELECT * FROM orders WHERE amount > 1000  → returns rows with id=5, id=8
  ... (Transaction B inserts row with id=11, amount=1500, and commits)
  T2: SELECT * FROM orders WHERE amount > 1000  → returns rows with id=5, id=8, id=11
                                                    ↑ PHANTOM — new row appeared!
```

Locking the existing rows (id=5 and id=8) at T1 would not prevent the phantom, because the inserted row (id=11) didn't exist to be locked.

InnoDB prevents phantoms with **gap locks** and **next-key locks**:

- **Gap lock**: a lock on the *gap* between two index records, or before the first, or after the last. It does not lock any actual row — it prevents insertions into that gap.
- **Next-key lock**: a combination of a record lock on an index record and a gap lock on the gap immediately preceding it. It's the default lock type for range scans under REPEATABLE READ.

```
Index records for `amount`:
   ... [800] [1200] [1500] [2000] ...
         ↑       ↑       ↑
       gap      gap     gap

A range scan for amount > 1000 would take next-key locks:
  - Lock on record [1200] + gap before [1200]
  - Lock on record [1500] + gap before [1500]
  - Lock on record [2000] + gap before [2000]
  - Lock on the gap after [2000] (supremum)

Now Transaction B tries to INSERT amount=1100 (falls in gap before [1200]):
  → Blocked! Gap lock held by Transaction A.
```

Gap locks block only *inserts into that gap*. They do not block other transactions from taking their own gap locks on overlapping ranges (two gap locks are compatible). They only conflict with insert-intention locks.

#### Why InnoDB Needs Gap Locks but PostgreSQL Doesn't

PostgreSQL's SSI detects phantoms at commit time by tracking read-write conflicts (if Transaction A read a range and Transaction B inserted into that range and committed, SSI will abort one of them). This is a validation-based approach: no locks during execution, abort at commit.

InnoDB's REPEATABLE READ uses a pessimistic, locking-based approach: it prevents the phantom from occurring at all by blocking the conflicting insert. This avoids aborts but can cause deadlocks and reduces concurrency when many transactions scan overlapping ranges.

Gap locks can be disabled by switching to READ COMMITTED isolation level or using `SELECT ... FOR UPDATE SKIP LOCKED`. Under READ COMMITTED, InnoDB only uses record locks (no gap locks), and phantoms are accepted (each statement gets a fresh read view, so phantoms within a single statement are prevented, but not across statements in the same transaction).

---

## 4. Design Trade-Offs

### 4.1 Clustered Index: Great for Locality, Painful for Inserts

**Benefit**: Primary key range scans are physically sequential. This is the single biggest performance win for OLTP queries that access rows by PK range.

**Cost — Insert Order Matters**: If the primary key is a non-monotonic value (e.g., a UUID), inserts go into random positions in the B+ tree. This causes frequent **page splits**: InnoDB must split a full leaf page to insert into the middle, which is expensive and leaves pages half-full. Disk access becomes random. Auto-increment integer PKs avoid this because new rows always append to the end of the rightmost leaf page — no splits except at the right boundary.

**Cost — Secondary Indexes Are More Expensive**: The double lookup cost (secondary index → PK → clustered index) is not free. On tables with many secondary indexes, writes must update every index, and reads via secondary indexes pay the double lookup penalty.

### 4.2 In-Place Updates + Undo: Good Write Amplification, Complex GC

**Benefit**: Write amplification for small updates is low — you only write the changed bytes to the redo log plus a compact undo record.

**Cost — Long-Running Transactions**: A single long-running read transaction holds a read view that prevents the purge thread from reclaiming undo records. If a table is heavily written while a long read transaction is open (common in analytics), the undo tablespace can grow to gigabytes. The read path for rows with long undo chains becomes slow (traversing the chain). This is arguably the most common InnoDB operational problem.

**Cost — Purge Lag**: The purge thread is a background process. Under sustained high write load, it may fall behind, causing undo tablespace growth. There is no operator-triggered equivalent to PostgreSQL's explicit `VACUUM`.

### 4.3 Gap Locks: Consistency vs Concurrency

**Benefit**: Under REPEATABLE READ (the default), gap locks make phantoms impossible without aborting transactions. Applications that assume REPEATABLE READ semantics work correctly without needing to handle serialization failures.

**Cost**: Gap locks increase the surface area for deadlocks. Two transactions scanning overlapping ranges and then trying to insert can deadlock in ways that are non-obvious to developers. `INSERT ... ON DUPLICATE KEY UPDATE` is notorious for gap lock deadlocks under concurrent load.

**The trade-off decision**: InnoDB chose safety-by-default (prevent phantoms, tolerate higher deadlock rate). PostgreSQL chose optimism-by-default (accept potential aborts at SERIALIZABLE, higher concurrency at lower isolation levels).

### 4.4 Buffer Pool LRU: Protects Hot Data, May Delay Eviction of Cold Data

The midpoint insertion policy prevents scan pollution effectively, but pages in the old sublist that are accessed multiple times within `innodb_old_blocks_time` are promoted to young. This threshold (1 second by default) can cause cache thrashing if a scan is slow (reads 1000 pages over 2 seconds, each page accessed once per second → promoted to young despite being a scan). Tuning `innodb_old_blocks_time` higher helps for known slow scans.

### 4.5 Redo Log Size: Throughput vs Recovery Time

A larger redo log allows InnoDB to defer checkpoint flushes longer, which means the write-heavy foreground operations aren't blocked by checkpoint advancement. But a larger redo log means longer crash recovery (more records to replay). This is a classic throughput-vs-recovery-time trade-off. MySQL 8.0 allows dynamic resizing, which helps tune this at runtime.

---

## 5. Experiments / Observations

### 5.1 SHOW ENGINE INNODB STATUS: Buffer Pool and Transaction Internals

Running `SHOW ENGINE INNODB STATUS\G` exposes real-time InnoDB internals. Key sections:

```
----------------------
BUFFER POOL AND MEMORY
----------------------
Total large memory allocated 137428992
Dictionary memory allocated 408929
Buffer pool size   8192       ← number of 16KB pages = 128MB
Free buffers       6987
Database pages     1195
Old database pages 420        ← pages in old sublist (~35%)
Modified db pages  38         ← dirty pages awaiting flush
Pending reads      0
Pending writes: LRU 0, flush list 0, single page 0
Pages made young 38294, not young 9821   ← promotion vs non-promotion
0.00 youngs/s, 0.00 non-youngs/s
Pages read 1178, created 17, written 1251
0.00 reads/s, 0.00 creates/s, 0.00 writes/s
Buffer pool hit rate 999 / 1000          ← 99.9% cache hit rate
Young-making rate 0 / 1000 not 0 / 1000
Pages read ahead 0.00/s, evicted without access 0.00/s
LRU len: 1195, unzip_LRU len: 0
I/O sum[0]:cur[0], unzip sum[0]:cur[0]
```

Observations:
- `Old database pages` is ~35% of total, consistent with the 3/8 old sublist target.
- `Pages made young` (38294) >> `not young` (9821) — most accessed pages are getting promoted to young sublist, suggesting a healthy access pattern (not dominated by scans).
- Buffer pool hit rate of 999/1000 means only 1 in 1000 page accesses required a disk read — the working set fits in buffer pool.

```
---TRANSACTION 421954836940464, not started
0 lock struct(s), heap size 1136, 0 row lock(s)
---TRANSACTION 1005, ACTIVE 312 sec
2 lock struct(s), heap size 1136, 1 row lock(s)
MySQL thread id 42, OS thread handle 123145..., query id 1823
```

A transaction active for 312 seconds is a potential undo log retention problem. Check `information_schema.INNODB_TRX` for long-running transactions.

---

### 5.2 EXPLAIN: Clustered Index vs Secondary Index

Setup:
```sql
CREATE TABLE orders (
  id       INT          NOT NULL AUTO_INCREMENT,
  user_id  INT          NOT NULL,
  amount   DECIMAL(10,2),
  status   VARCHAR(20),
  PRIMARY KEY (id),
  INDEX idx_user (user_id),
  INDEX idx_status_amount (status, amount)
);
```

**Query 1: Primary key range scan (clustered index)**

```sql
EXPLAIN SELECT id, user_id, amount, status
FROM orders
WHERE id BETWEEN 1000 AND 2000;
```

```
+----+-------------+--------+-------+---------------+---------+------+--------+-------------+
| id | select_type | table  | type  | possible_keys | key     | rows | Extra  |             |
+----+-------------+--------+-------+---------------+---------+------+--------+-------------+
|  1 | SIMPLE      | orders | range | PRIMARY       | PRIMARY | 1001 |        |             |
+----+-------------+--------+-------+---------------+---------+------+--------+-------------+
```

`type=range` on `key=PRIMARY` means the engine walks the clustered index between id=1000 and id=2000. All columns requested are in the clustered index leaf page — no secondary lookup needed. This is maximally efficient.

**Query 2: Secondary index with double lookup**

```sql
EXPLAIN SELECT id, user_id, amount, status
FROM orders
WHERE user_id = 42;
```

```
+----+-------------+--------+------+---------------+----------+------+--------+------------------+
| id | select_type | table  | type | possible_keys | key      | rows | Extra  |                  |
+----+-------------+--------+------+---------------+----------+------+--------+------------------+
|  1 | SIMPLE      | orders | ref  | idx_user      | idx_user | 37   |        |                  |
+----+-------------+--------+------+---------------+----------+------+--------+------------------+
```

`type=ref` on `key=idx_user` — uses the secondary index. For each of the 37 rows found in `idx_user`, InnoDB fetches the PK, then looks up the full row in the clustered index (double lookup). If the user had 10,000 orders, the optimizer might switch to a full clustered scan if it estimated the double lookup cost to be worse.

**Query 3: Covering index (no double lookup)**

```sql
EXPLAIN SELECT user_id, amount
FROM orders
WHERE user_id = 42;
```

Wait — `amount` is not in `idx_user`. But if we use `idx_status_amount` won't cover either. Let's say we add a covering index:

```sql
ALTER TABLE orders ADD INDEX idx_user_covering (user_id, amount, status);

EXPLAIN SELECT user_id, amount, status
FROM orders WHERE user_id = 42;
```

```
+----+-------------+--------+------+---------------------+---------------------+------+--------+-------------+
| id | select_type | table  | type | possible_keys       | key                 | rows | Extra  |             |
+----+-------------+--------+------+---------------------+---------------------+------+--------+-------------+
|  1 | SIMPLE      | orders | ref  | idx_user_covering   | idx_user_covering   | 37   |        | Using index |
+----+-------------+--------+------+---------------------+---------------------+------+--------+-------------+
```

`Extra: Using index` means InnoDB answered the query entirely from the secondary index leaf page without touching the clustered index. This is a covering index scan — the double lookup is eliminated.

---

### 5.3 Demonstrating Gap Lock Behavior

Setup: an `accounts` table with a gap between id=10 and id=20.

```sql
CREATE TABLE accounts (id INT PRIMARY KEY, balance INT);
INSERT INTO accounts VALUES (1, 100), (10, 500), (20, 300), (30, 900);
-- Gap: (10, 20), (20, 30) exist as gaps
```

Session A:
```sql
BEGIN;
SELECT * FROM accounts WHERE id BETWEEN 10 AND 20 FOR UPDATE;
-- Takes next-key locks on: (-∞,1], (1,10], (10,20], (20,20]
-- Specifically, gap lock on (10, 20) is held
```

Session B (concurrent):
```sql
BEGIN;
INSERT INTO accounts VALUES (15, 200);  -- id=15 falls in gap (10, 20)
-- BLOCKS — waiting for gap lock held by Session A
```

```
ERROR 1205 (HY000): Lock wait timeout exceeded; try restarting transaction
```

Session B cannot insert into the gap while Session A holds the gap lock. If Session A commits, Session B's insert proceeds immediately.

**Contrast with READ COMMITTED**:
```sql
-- Session A (READ COMMITTED):
SET TRANSACTION ISOLATION LEVEL READ COMMITTED;
BEGIN;
SELECT * FROM accounts WHERE id BETWEEN 10 AND 20 FOR UPDATE;
-- Takes ONLY record locks on id=10 and id=20, NO gap locks
```

Session B's insert of id=15 would succeed immediately — READ COMMITTED doesn't prevent phantoms across statements, but it eliminates gap lock contention.

**Checking locks held**:
```sql
SELECT ENGINE_LOCK_TYPE, INDEX_NAME, LOCK_MODE, LOCK_DATA
FROM performance_schema.data_locks
WHERE OBJECT_NAME = 'accounts';
```

```
+------------------+------------+-----------+-----------+
| ENGINE_LOCK_TYPE | INDEX_NAME | LOCK_MODE | LOCK_DATA |
+------------------+------------+-----------+-----------+
| TABLE            | NULL       | IX        | NULL      |
| RECORD           | PRIMARY    | X         | 10        |
| RECORD           | PRIMARY    | X,GAP     | 20        |  ← gap lock before 20
| RECORD           | PRIMARY    | X         | 20        |
| RECORD           | PRIMARY    | X,GAP     | supremum  |  ← gap after last record
+------------------+------------+-----------+-----------+
```

The `X,GAP` mode confirms gap locks are held on the (10,20) interval and after 20. These are the locks that would block Session B's insert of id=15.

---

## 6. Key Learnings

### L1: Physical Clustering Has Outsized Impact on Real Workloads

The decision to make the clustered index the table is not just a storage optimization — it changes the I/O profile of almost every query. The difference between 1 I/O (primary key lookup) and 3 I/Os (secondary index → PK → clustered) matters enormously at scale. The practical implication: choose your primary key with range access patterns in mind, and design secondary indexes to be covering where possible.

### L2: The Undo Log Is the Linchpin of Everything Transactional

Undo logs are not just a rollback mechanism — they are the version store that makes MVCC possible. Understanding that undo records form a linked version history of every row explains why long-running transactions are operationally dangerous in InnoDB: they don't just hold row locks, they prevent garbage collection of the entire version history of every row touched since the transaction's read view was created.

### L3: Redo Log and Undo Log Solve Different Parts of Durability

A common confusion is thinking WAL alone solves all of crash recovery. In InnoDB: the redo log ensures committed changes survive crashes; the undo log ensures uncommitted changes are rolled back on recovery. Without undo, a crash during a transaction would leave partial changes in data pages, and redo replay would replay those partial changes. The interplay of redo-then-undo in crash recovery is the ARIES algorithm, and InnoDB implements it faithfully.

### L4: The Buffer Pool LRU Is Not "Just an LRU"

The midpoint insertion policy is a purposeful defense against a known pathology. This is a good example of how database systems accumulate targeted complexity: each addition (old/young sublists, `innodb_old_blocks_time`, AHI enable/disable) exists to handle a specific failure mode discovered in production. Understanding the motivation behind each mechanism is more valuable than memorizing the defaults.

### L5: Gap Locks Trade Concurrency for Correctness (and It's a Deliberate Choice)

The existence of gap locks reflects a philosophical choice: InnoDB prevents phantoms by blocking conflicting operations, not by aborting conflicting transactions. This means applications on InnoDB's REPEATABLE READ never need to handle serialization failures (no `40001` errors to retry), but they must handle deadlocks (`1213` errors). PostgreSQL's SSI inverts this trade-off. Neither is universally better; the right choice depends on whether your application can tolerate retries better than it can tolerate deadlocks.

### L6: InnoDB's Design Is Optimized for OLTP Write-Read Mix

The overall design — fast primary key access, in-place updates with low write amplification, aggressive buffer pool caching, row-level locking — is tuned for the OLTP sweet spot: many concurrent transactions, mostly small reads and writes, short-lived transactions, row-level contention rather than table-level. For analytics (large sequential scans, long transactions, high concurrency on the same rows), the trade-offs tilt unfavorably: scans pollute the buffer pool (mitigated but not eliminated), long transactions cause undo bloat, and gap locks on range scans reduce insert throughput. Understanding these limits is as important as understanding the strengths.

---

*Advanced DBMS Assignment — MySQL / InnoDB Storage Engine Deep Dive*
