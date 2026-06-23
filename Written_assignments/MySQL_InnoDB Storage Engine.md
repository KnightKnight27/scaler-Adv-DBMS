# MySQL / InnoDB Storage Engine

> A deep-dive into InnoDB's architecture — clustered indexes, buffer pool, undo/redo logging, MVCC, and locking — with explicit analysis of how and why InnoDB's design differs from PostgreSQL's.

---

## Table of Contents

1. [Clustered Indexes & Primary Key Storage](#1-clustered-indexes--primary-key-storage)
2. [Secondary Indexes](#2-secondary-indexes)
3. [Buffer Pool](#3-buffer-pool)
4. [Undo Logs](#4-undo-logs)
5. [Redo Logs](#5-redo-logs)
6. [MVCC: Oracle-Style Concurrency](#6-mvcc-oracle-style-concurrency)
7. [Row-Level Locking & Gap Locks](#7-row-level-locking--gap-locks)
8. [Transaction Processing & Isolation Levels](#8-transaction-processing--isolation-levels)
9. [InnoDB vs PostgreSQL: Architectural Comparison](#9-innodb-vs-postgresql-architectural-comparison)

---

## 1. Clustered Indexes & Primary Key Storage

### What Is a Clustered Index?

In InnoDB, **every table is a B+-Tree** — not a heap file. The table's rows are physically stored inside the B+-Tree leaf pages, ordered by the primary key. This is called a **clustered index** because the data and the index are the same structure.

```
InnoDB Clustered Index (B+-Tree, ordered by PRIMARY KEY)

                        [ Root: PK ranges ]
                       /                   \
          [Internal: PK 1–500]      [Internal: PK 501–1000]
               /         \                  /          \
      [Leaf: PK 1–100]  [Leaf: PK 101–200] ... [Leaf: PK 901–1000]
      ┌─────────────────────────────┐
      │ PK=1 | name="Alice" | age=30│  ← Full row data lives here
      │ PK=2 | name="Bob"   | age=25│
      │ ...                         │
      └─────────────────────────────┘
```

Contrast this with PostgreSQL's **heap storage**: the table data lives in an unordered heap file, and B-Tree indexes are separate structures containing `(key, TID)` pairs that point back to the heap.

### Why Clustered Indexes Improve Lookup Performance

**Primary key lookups are faster** because a single B-Tree traversal retrieves the full row — no second lookup required. In PostgreSQL, an index scan requires: (1) traverse B-Tree to find TID, then (2) go to the heap page to fetch the row. InnoDB does it in one step.

**Range scans on the primary key are especially efficient.** Because rows are physically stored in PK order, scanning `WHERE id BETWEEN 100 AND 200` reads a contiguous range of leaf pages — excellent for disk I/O locality.

```
PostgreSQL index lookup (2 I/Os):          InnoDB clustered lookup (1 I/O):
  B-Tree → TID (block 42, offset 7)          B-Tree leaf page → full row data
      ↓
  Heap page 42 → actual row data
```

### The Primary Key Choice Matters

Because every row's physical location is its PK position in the B+-Tree, the choice of primary key has architectural consequences:

- **Sequential integer PKs** (e.g., `AUTO_INCREMENT`): inserts always go to the rightmost leaf page — low contention, minimal page splits, excellent write performance.
- **Random PKs** (e.g., UUIDs v4): inserts scatter across the entire tree, causing frequent page splits and random I/O — a major performance problem at scale. UUID v7 (time-ordered) is commonly recommended as a fix.
- **No explicit PK**: InnoDB silently creates a hidden 6-byte `rowid` column as the clustered key — you lose the ability to control clustering behavior.

**Trade-off:** The clustered design is excellent for PK-based access patterns but means that changing the primary key requires rebuilding the entire table, and non-PK inserts on large tables can cause significant write amplification.

---

## 2. Secondary Indexes

Since the clustered index already stores rows by PK, secondary indexes cannot also store the full row — that would require data duplication for every index.

Instead, **InnoDB secondary indexes store the indexed column(s) plus the primary key value** (not a physical pointer like PostgreSQL's TID):

```
Secondary Index on (last_name):
  Leaf page entry: last_name="Smith" → PK=42

Secondary lookup requires TWO B-Tree traversals:
  1. Secondary index B-Tree: "Smith" → PK=42
  2. Clustered index B-Tree: PK=42 → full row data
```

This two-step process is called a **double lookup** or **bookmark lookup**.

### Covering Indexes

If the query only needs columns that are all present in the secondary index (including the PK), InnoDB can satisfy the query using only the secondary index without touching the clustered index. This is called a **covering index** and is a common optimization:

```sql
-- Index on (last_name, first_name) covers this query:
SELECT last_name, first_name, id FROM users WHERE last_name = 'Smith';
-- No clustered index lookup needed — all needed columns are in the secondary index
```

**Trade-off vs PostgreSQL:** PostgreSQL's TID-based secondary indexes are cheaper to update (just change the TID if the row moves) but become invalid if the heap row is moved. InnoDB's PK-based secondary indexes never go stale (the PK doesn't change), but every secondary lookup costs an extra clustered index traversal.

---

## 3. Buffer Pool

The InnoDB Buffer Pool is functionally similar to PostgreSQL's shared buffers but with several architectural differences.

### Structure

The buffer pool is divided into **pages** (default 16KB, vs PostgreSQL's 8KB) organized in a modified **LRU list**. Unlike PostgreSQL's Clock Sweep, InnoDB uses a **two-sublist LRU**:

```
Buffer Pool LRU List:
┌─────────────────────────────────────────────────────┐
│        New Sublist (5/8 of pool)  │  Old Sublist (3/8) │
│  ← Most recently accessed pages  │  Candidate eviction │
└─────────────────────────────────────────────────────┘
                                    ↑
                         New pages inserted here
```

When a page is first loaded (e.g., for a full table scan), it enters at the **midpoint** (boundary between new and old sublists). It only moves to the new sublist (protected from eviction) if it is accessed again within `innodb_old_blocks_time` (default 1 second). This prevents large table scans from evicting "hot" working-set pages — a problem classical LRU suffers from.

### Change Buffer

A unique InnoDB feature: when a secondary index page is not in the buffer pool, InnoDB **buffers the write** (insert, delete, update to the secondary index) in a special structure called the **Change Buffer** instead of loading the page just to apply the change. These buffered changes are merged into the actual index pages later when the page is loaded for another reason.

This dramatically reduces random I/O for write-heavy workloads.

**Trade-off:** The Change Buffer adds complexity to crash recovery — buffered changes must also be persisted (they are, via the redo log) and merged before the index page can be trusted to be current.

---

## 4. Undo Logs

### Purpose

Undo logs serve two distinct purposes in InnoDB:

1. **Transaction rollback** — if a transaction is aborted, undo log entries describe how to reverse each change (the "before image" of every modified row)
2. **MVCC read consistency** — when a transaction reads a row whose current version is newer than its snapshot, InnoDB traverses the undo log chain to reconstruct an older version

### How Undo Logs Work

When a row is updated, InnoDB:
1. Writes the **old row version** (before image) to the undo log segment in the system tablespace (or a dedicated undo tablespace)
2. **Overwrites the row in-place** in the clustered index leaf page with the new values
3. Adds a pointer in the updated row's header back to the undo log entry (the **roll pointer**)

```
Clustered Index Leaf Page (after UPDATE):
┌─────────────────────────────────────────────────────┐
│ PK=42 | trx_id=500 | roll_ptr=→ | name="Alice" | salary=60000 │  ← current
└───────────────────────────────────────┬─────────────┘
                                        │ roll_ptr
                                        ↓
                               Undo Log Entry:
                               ┌────────────────────────────────┐
                               │ trx_id=400 | name="Alice" | salary=50000 │  ← previous version
                               └────────────────────────────────┘
```

This forms a **version chain** — a linked list of undo log entries representing all historical versions of a row. A transaction needing an older snapshot traverses this chain until it finds a version that was committed before the transaction's read snapshot.

### Undo Log Purge

Once no active transaction needs an undo log entry (no snapshot is old enough to require it), InnoDB's **purge thread** removes it. This is analogous to PostgreSQL's VACUUM — but crucially, InnoDB's dead versions live in a separate undo space, not in the clustered index itself. This means InnoDB avoids **heap bloat** (dead tuples mixed with live tuples in the same page) at the cost of maintaining a separate undo tablespace.

---

## 5. Redo Logs

### Purpose

Redo logs serve a different purpose from undo logs: they ensure **durability**. The redo log records what changes were made so they can be replayed after a crash.

### Why InnoDB Needs Both Undo AND Redo Logs

This is the most commonly misunderstood aspect of InnoDB's design. The two logs solve two entirely separate problems:

| Log | Solves | Contains |
|-----|--------|----------|
| **Undo Log** | Atomicity (rollback) + MVCC (old snapshots) | Before-images of changed rows |
| **Redo Log** | Durability (crash recovery) | After-images / physical changes to pages |

They are not redundant. If a transaction commits and then the system crashes:
- The **redo log** ensures the committed changes survive (replayed on restart)
- The **undo log** ensures in-progress (uncommitted) changes at crash time are rolled back on restart

### Redo Log Structure

InnoDB's redo log is a **circular buffer** of fixed-size files (e.g., `ib_logfile0`, `ib_logfile1`). Each redo log record describes a physical change to a page — a lower-level description than PostgreSQL's WAL records.

Before any in-place modification to a buffer pool page, InnoDB writes a redo log record. On commit, the redo log up to the commit LSN is flushed to disk (fsync). This is the same WAL principle as PostgreSQL — **log before data** — but the redo log is a separate structure from the undo log, whereas PostgreSQL's WAL subsumes both roles.

### Crash Recovery with Both Logs

On restart after a crash:
1. **Redo phase**: Replay all redo log records from the last checkpoint LSN forward. This brings all modified pages (committed or not) to their state just before the crash.
2. **Undo phase**: Identify transactions that were in-progress at crash time (using undo log records), and roll them back by applying the undo log entries in reverse.

PostgreSQL skips the undo phase entirely — because MVCC tuple versioning means uncommitted changes are simply invisible (their `xmin` never committed), so nothing needs to be reversed.

---

## 6. MVCC: Oracle-Style Concurrency

### InnoDB's Approach vs PostgreSQL's

Both InnoDB and PostgreSQL implement MVCC to allow readers and writers to not block each other, but they do it in fundamentally different ways:

| Dimension | PostgreSQL | InnoDB |
|-----------|-----------|--------|
| Where old versions live | In the heap (same page as current row) | In a separate undo log |
| Current row storage | Append-only (new tuple written alongside old) | In-place update |
| Cleanup mechanism | VACUUM cleans dead tuples from heap | Purge thread cleans undo log entries |
| Heap/clustered index bloat | Yes — dead tuples accumulate in heap pages | No — clustered index always has the current version |
| Old version reconstruction | Old tuple is directly in heap (no reconstruction) | Must traverse roll_ptr chain in undo log |

### Read View (Snapshot)

When a transaction in `REPEATABLE READ` isolation begins, InnoDB creates a **read view** (equivalent to PostgreSQL's snapshot) containing:
- `trx_id_min`: The smallest active transaction ID — any row with `trx_id < trx_id_min` is definitely visible
- `trx_id_max`: The next transaction ID to be assigned — any row with `trx_id >= trx_id_max` is invisible
- `trx_ids`: Set of active (uncommitted) transaction IDs at the time of the read view

When reading a row, InnoDB checks the row's `trx_id` field. If it's not visible under the read view, InnoDB follows the `roll_ptr` into the undo log, checks that version's `trx_id`, and repeats until it finds a visible version.

### Why PostgreSQL Chose a Different Model

PostgreSQL's append-only model was a deliberate choice rooted in simplicity and safety:

1. **No separate undo space to manage or tune** — all versions live in the same tablespace as the data, simplifying storage management
2. **No undo phase on crash recovery** — uncommitted changes are naturally invisible via `xmin` visibility checks; no rollback-on-restart logic needed
3. **Simpler code path for reads** — the old row version is directly accessible in the heap; no undo log traversal is required

The cost is heap bloat and the need for VACUUM. PostgreSQL accepted this trade-off because it simplifies the storage engine considerably and avoids the undo log becoming a write bottleneck (InnoDB's undo tablespace can become a contention point under heavy write workloads with long-running transactions).

---

## 7. Row-Level Locking & Gap Locks

### Row-Level Locking

InnoDB implements locking at the **row level** (unlike MyISAM which locks the entire table). Each row that is read or written in a transaction can be locked independently. InnoDB stores locks in a lock manager (in memory), not in the rows themselves.

**Types of row locks:**
- **Shared (S) lock**: Multiple transactions can hold S locks on the same row simultaneously. Used for `SELECT ... FOR SHARE`.
- **Exclusive (X) lock**: Only one transaction can hold an X lock. Used for `UPDATE`, `DELETE`, `SELECT ... FOR UPDATE`. Blocks other X and S locks.

### Gap Locks

A critical difference from PostgreSQL: InnoDB uses **gap locks** to prevent **phantom reads** (a new row appearing in a repeated range query within the same transaction). A gap lock locks the *gap between index entries*, preventing any other transaction from inserting a row in that range.

```
Index values: ... [10] [20] [30] [40] ...

SELECT * FROM orders WHERE order_id BETWEEN 15 AND 25 FOR UPDATE;

Gap locks acquired:
  - Gap lock on (10, 20) — no insert of id=11..19 by any other transaction
  - Record lock on id=20
  - Gap lock on (20, 30) — no insert of id=21..29 by any other transaction
```

### Next-Key Locks

InnoDB's default locking in `REPEATABLE READ` uses **next-key locks** — a combination of a record lock on the index entry and a gap lock on the gap before it. This prevents both the record being modified and new records being inserted into adjacent gaps.

**Trade-off:** Gap locks can cause unexpected deadlocks and reduce concurrency in insert-heavy workloads. If two transactions hold gap locks on overlapping ranges and each tries to insert into the other's gap, a deadlock results.

PostgreSQL avoids gap locks entirely — it uses **predicate locking** (tracking what ranges were read) in `SERIALIZABLE` isolation, and in `REPEATABLE READ`, phantom reads are actually allowed (PostgreSQL's `REPEATABLE READ` behaves like snapshot isolation, not true REPEATABLE READ).

---

## 8. Transaction Processing & Isolation Levels

### InnoDB's Default: REPEATABLE READ

InnoDB defaults to `REPEATABLE READ` with next-key locking. A read view is created at the **first read in the transaction** and held for the entire transaction, so repeated reads return the same snapshot.

### Isolation Levels Compared

| Level | Dirty Read | Non-Repeatable Read | Phantom Read | InnoDB Implementation |
|-------|------------|--------------------|--------------|-----------------------|
| `READ UNCOMMITTED` | Possible | Possible | Possible | No locking; reads latest version of row regardless of commit status |
| `READ COMMITTED` | Not possible | Possible | Possible | New read view created per statement; no gap locks |
| `REPEATABLE READ` (default) | Not possible | Not possible | Not possible* | Read view held for transaction lifetime; next-key locks prevent phantoms |
| `SERIALIZABLE` | Not possible | Not possible | Not possible | All reads become `FOR SHARE`; full two-phase locking |

*InnoDB's `REPEATABLE READ` prevents phantom reads via gap locks for locking reads (`FOR UPDATE`/`FOR SHARE`), but consistent (non-locking) reads use MVCC snapshots and don't see phantoms anyway.

### Transaction Commit Flow

When a transaction commits:
1. Undo log entries are marked as "committed" but not immediately purged (still needed for older read views)
2. Redo log records up to the commit LSN are flushed to disk (`fsync`)
3. Locks are released
4. The purge thread eventually removes undo log entries no longer needed by any active read view

---

## 9. InnoDB vs PostgreSQL: Architectural Comparison

### The Fundamental Architectural Fork

Both engines implement MVCC and WAL-style durability, but made opposite choices on one core question: **where do old row versions live?**

```
PostgreSQL (Append-Only Heap):              InnoDB (In-Place + Undo Log):

Heap Page:                                  Clustered Index Page:
┌────────────────────────────┐              ┌──────────────────────────┐
│ Tuple v1: xmin=100,xmax=200│ ← dead       │ PK=1 | trx_id=200 | ←   │ ← always current
│ Tuple v2: xmin=200,xmax=0  │ ← live       │    salary=60000          │
└────────────────────────────┘              └──────────────┬───────────┘
                                                           │ roll_ptr
                                            Undo Segment:  ↓
                                            ┌──────────────────────────┐
                                            │ trx_id=100 | salary=50000│ ← old version
                                            └──────────────────────────┘
```

### Detailed Trade-Off Analysis

**Clustered Index vs. Heap:**

| | InnoDB | PostgreSQL |
|---|---|---|
| PK lookup | Single B-Tree traversal | B-Tree → TID → heap (2 I/Os) |
| Secondary lookup | Secondary B-Tree → PK → Clustered B-Tree (2 traversals) | B-Tree → TID → heap (2 I/Os) |
| Range scan on PK | Excellent (physically ordered) | Requires sort or index scan + heap fetch |
| Row update cost | In-place (+ undo log write) | New tuple appended to heap |
| Table scan bloat | No (only current version in clustered index) | Yes (dead tuples remain until VACUUM) |

**MVCC Implementation:**

| | InnoDB | PostgreSQL |
|---|---|---|
| Old version access | Traverse undo log chain (indirection) | Read directly from heap page (direct) |
| Write I/O for UPDATE | Undo log write + in-place page modify | New tuple appended (no undo log) |
| Bloat location | Undo tablespace | Heap pages |
| Cleanup | Purge thread (automatic, background) | VACUUM (must be tuned, scheduled) |
| Crash recovery undo | Undo phase on restart (explicit rollback) | Not needed (uncommitted = invisible) |

**Why PostgreSQL's Model Can Be Simpler:**
- No undo tablespace to size, monitor, or tune
- No undo phase in crash recovery (which can take a long time if long transactions were open at crash)
- Long-running transactions don't cause undo log growth that blocks other transactions' purge

**Why InnoDB's Model Can Be Better:**
- No heap bloat — clustered index pages stay dense with live data
- Consistent read performance regardless of how many dead versions exist (they're off-page in undo log)
- No need for an explicit VACUUM operation (though undo log purge can fall behind with long transactions)

### Answering the Suggested Questions

**Why does InnoDB need both undo and redo logs?**

They solve orthogonal problems. The redo log guarantees **durability** — it records what changes were made so committed work survives crashes (replayed forward). The undo log guarantees **atomicity and read consistency** — it stores before-images of rows for rollback and for MVCC readers who need older snapshots (applied backward). PostgreSQL's WAL subsumes redo-log duties, but it has no separate undo log because old versions live in-place in the heap.

**What advantages do clustered indexes provide?**

Primary key lookups retrieve the full row in a single B-Tree traversal (vs. PostgreSQL's two-step index + heap fetch). Range scans on the PK benefit from physical data locality. There is no heap bloat — deleted rows are removed in-place and their space is reclaimed without a separate VACUUM sweep of the data pages.

**Why did PostgreSQL choose a different MVCC model?**

PostgreSQL's append-only heap model simplifies the storage engine: no separate undo tablespace, no crash-recovery undo phase, and old versions are directly accessible without pointer chasing. The accepted trade-off — heap bloat requiring VACUUM — was considered manageable compared to the operational complexity of a separate undo log (which must be sized appropriately and can become a bottleneck when long-running transactions hold read views that prevent purge from running).

---

## Summary: Key Trade-offs

| Design Choice | Benefit | Cost |
|---|---|---|
| Clustered index (table = B-Tree) | Fast PK lookups, good range scan locality | Rebuilding table = rebuilding entire clustered index; random PK inserts cause splits |
| Secondary indexes store PK (not TID) | Secondary indexes never go stale | Every secondary lookup requires an extra clustered index traversal |
| In-place updates + undo log | No heap bloat; clustered index stays dense | Undo tablespace overhead; undo phase in crash recovery; long transactions delay purge |
| Gap locks for phantom prevention | True REPEATABLE READ semantics | Higher deadlock potential; reduced insert concurrency |
| Two-sublist LRU buffer pool | Table scans don't evict hot pages | More complex eviction logic than Clock Sweep |
| Separate undo + redo logs | Clean separation of concerns (rollback vs. durability) | Two logs to manage, write, and tune |

---

*Analyzed by studying InnoDB internals, MySQL source architecture documentation, and comparative database design literature. Comparisons with PostgreSQL are based on Topic 2 analysis.*
