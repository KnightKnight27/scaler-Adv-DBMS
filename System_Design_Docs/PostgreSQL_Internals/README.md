# PostgreSQL Internal Architecture — Deep Dive

## 1. Problem Background

### Why Study PostgreSQL Internals?

PostgreSQL is often described as "the world's most advanced open-source relational database." But what does "advanced" actually mean at the systems level? It means decades of careful engineering around four core challenges that every database must solve:

1. **How to manage memory efficiently** when the database is much larger than RAM (Buffer Manager)
2. **How to find data quickly** without scanning every row (B-Tree indexes)
3. **How to allow concurrent access** without corrupting data or returning inconsistent results (MVCC)
4. **How to survive crashes** without losing committed data (Write-Ahead Logging)

PostgreSQL's architecture was born from the POSTGRES project (1986) at UC Berkeley under Michael Stonebraker. The key design philosophy was **extensibility**: the system should be modifiable by users without changing the core engine. This philosophy drove decisions like the catalog-driven type system, loadable extensions, and the pluggable index access method interface.

Understanding PostgreSQL's internals isn't just academic — it directly explains:
- Why `VACUUM` exists and when it becomes a problem
- Why certain queries use index scans while others use sequential scans
- Why `shared_buffers` tuning matters so much for performance
- Why long-running transactions can cause table bloat

---

## 2. Architecture Overview

### High-Level System Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                       PostgreSQL Server                                 │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────┐        │
│  │                    Postmaster Process                        │        │
│  │  - Listens on port 5432                                     │        │
│  │  - Authenticates connections (pg_hba.conf)                  │        │
│  │  - fork()s backend processes                                │        │
│  └─────────┬───────────────────────────────────────────────────┘        │
│            │ fork()                                                      │
│  ┌─────────▼───────────────────────────────────────────────────┐        │
│  │               Backend Process (per connection)               │        │
│  │  ┌─────────┐  ┌──────────┐  ┌───────────┐  ┌────────────┐  │        │
│  │  │ Parser  │→│ Analyzer │→│ Planner/  │→│ Executor   │  │        │
│  │  │         │  │ (Rewrite)│  │ Optimizer │  │            │  │        │
│  │  └─────────┘  └──────────┘  └───────────┘  └─────┬──────┘  │        │
│  │                                                    │         │        │
│  │  Private Memory:                                   │         │        │
│  │  - work_mem (sort/hash operations)                │         │        │
│  │  - temp_buffers (temp table access)               │         │        │
│  │  - maintenance_work_mem (VACUUM, CREATE INDEX)    │         │        │
│  └──────────────────────────────────────────────┬────┘         │        │
│                                                  │              │        │
│  ┌──────────────────────────────────────────────▼──────────────┐│       │
│  │                   Shared Memory                              ││       │
│  │                                                              ││       │
│  │  ┌──────────────────────────────────────────────────┐       ││       │
│  │  │          Shared Buffer Pool                       │       ││       │
│  │  │   ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐      │       ││       │
│  │  │   │Buf 0│ │Buf 1│ │Buf 2│ │ ... │ │Buf N│      │       ││       │
│  │  │   │8 KB │ │8 KB │ │8 KB │ │     │ │8 KB │      │       ││       │
│  │  │   └─────┘ └─────┘ └─────┘ └─────┘ └─────┘      │       ││       │
│  │  │   Buffer Descriptors  │  Hash Table (tag→buf)   │       ││       │
│  │  └──────────────────────────────────────────────────┘       ││       │
│  │                                                              ││       │
│  │  ┌─────────────────┐  ┌─────────────────┐                  ││       │
│  │  │  WAL Buffers     │  │  Lock Tables    │                  ││       │
│  │  │  (wal_buffers)   │  │  (lightweight &  │                  ││       │
│  │  │                  │  │   heavyweight)   │                  ││       │
│  │  └─────────────────┘  └─────────────────┘                  ││       │
│  │                                                              ││       │
│  │  ┌─────────────────┐  ┌─────────────────┐                  ││       │
│  │  │ CLOG Buffers     │  │ Proc Array      │                  ││       │
│  │  │ (pg_xact cache)  │  │ (active txn list)│                 ││       │
│  │  └─────────────────┘  └─────────────────┘                  ││       │
│  └─────────────────────────────────────────────────────────────┘│       │
│                                                                  │       │
│  ┌──────────────────────────────────────────────────────────────┐│       │
│  │                Background Processes                          ││       │
│  │  ┌──────────┐ ┌────────────┐ ┌──────────┐ ┌──────────────┐ ││       │
│  │  │ BG Writer│ │Checkpointer│ │WAL Writer│ │  Autovacuum  │ ││       │
│  │  │          │ │            │ │          │ │  Launcher    │ ││       │
│  │  └──────────┘ └────────────┘ └──────────┘ └──────────────┘ ││       │
│  │  ┌──────────────┐  ┌────────────────┐                      ││       │
│  │  │Stats Collector│  │ Archiver       │                      ││       │
│  │  └──────────────┘  └────────────────┘                      ││       │
│  └──────────────────────────────────────────────────────────────┘│       │
│                              │                                    │       │
│                              ▼                                    │       │
│  ┌──────────────────────────────────────────────────────────────┐│       │
│  │                    Disk Storage                               ││       │
│  │                                                               ││       │
│  │  base/          - Database directories                        ││       │
│  │    <oid>/       - One dir per database                        ││       │
│  │      <relid>    - One file per table/index (1 GB segments)   ││       │
│  │                                                               ││       │
│  │  pg_wal/        - WAL segment files (16 MB each)             ││       │
│  │  pg_xact/       - Transaction commit status (2 bits per txn) ││       │
│  │  pg_stat/       - Statistics files                            ││       │
│  │  pg_tblspc/     - Tablespace symlinks                         ││       │
│  └──────────────────────────────────────────────────────────────┘│       │
└──────────────────────────────────────────────────────────────────────────┘
```

### Data Flow Through the System

A typical query like `SELECT * FROM users WHERE id = 42` flows through:

1. **Parser** → Converts SQL text to a parse tree. Validates syntax.
2. **Analyzer** → Resolves table/column names against the system catalog. Applies rewrite rules (e.g., view expansion).
3. **Planner/Optimizer** → Generates execution plans, estimates costs using statistics from `pg_statistic`, and selects the cheapest plan.
4. **Executor** → Executes the plan node by node, calling into the **Buffer Manager** to read/write pages, the **Index Access Methods** for index lookups, and the **Transaction Manager** for visibility checks.

---

## 3. Internal Design

### 3.1 Buffer Manager

**Source location:** `src/backend/storage/buffer/`

The Buffer Manager is PostgreSQL's **page cache** — it mediates all access between backend processes and the disk. No backend ever reads or writes disk directly; everything goes through shared buffers.

#### How It Works

```
Buffer Manager Architecture:

  Backend Process                    Shared Buffer Pool
  ┌─────────────┐                   ┌─────────────────────────────┐
  │ ReadBuffer()│───────────────── │  Hash Table                 │
  │             │  1. Compute tag   │  ┌─────────────────────────┐│
  │             │     (RelId,       │  │ (rel=16384, fork=0,     ││
  │             │      ForkNum,     │  │  blk=5) → Buffer #237  ││
  │             │      BlockNum)    │  └─────────────────────────┘│
  │             │                   │                              │
  │             │  2. Hash lookup   │  Buffer Descriptors          │
  │             │─────────────────▶│  ┌───────────────────────┐   │
  │             │                   │  │ Buf #237:             │   │
  │             │  3a. HIT:         │  │   tag: (16384,0,5)    │   │
  │             │  Pin buffer,      │  │   state: VALID|DIRTY  │   │
  │             │  return pointer   │  │   refcount: 3         │   │
  │             │                   │  │   usage_count: 4      │   │
  │             │  3b. MISS:        │  │   content_lock: SHARE │   │
  │             │  - Find victim    │  └───────────────────────┘   │
  │             │    (clock sweep)  │                              │
  │             │  - If victim is   │  Buffer Pages (8 KB each)    │
  │             │    dirty, write   │  ┌─────┐ ┌─────┐ ┌─────┐   │
  │             │    it to disk     │  │  0  │ │  1  │ │ 237 │   │
  │             │  - Read new page  │  │     │ │     │ │█████│   │
  │             │    from disk      │  │     │ │     │ │█████│   │
  │             │  - Update hash    │  └─────┘ └─────┘ └─────┘   │
  │             │    table          │                              │
  └─────────────┘                   └─────────────────────────────┘
```

#### Buffer Replacement: Clock-Sweep Algorithm

PostgreSQL uses a **clock-sweep** algorithm (a variant of the clock/second-chance algorithm) for buffer replacement:

1. Each buffer has a **usage_count** (0-5) and a **refcount** (number of backends currently using it).
2. When a page is accessed, its usage_count is incremented (up to 5).
3. When a free buffer is needed, the algorithm sweeps circularly through buffers:
   - Skip buffers with refcount > 0 (currently pinned).
   - If usage_count > 0, decrement it and move on.
   - If usage_count = 0, this buffer is the victim — evict it.

```
Clock Sweep Example:

  Buffers:  [A:3] [B:0] [C:1] [D:0] [E:2]
                    ↑
                  clock hand

  Need to evict a buffer:
  
  Step 1: B has usage_count=0 → Victim found!
          Evict B, load new page into this slot.
  
  If B had usage_count=2:
  Step 1: B:2 → decrement to B:1, move on
  Step 2: C:1 → decrement to C:0, move on  
  Step 3: D:0 → Victim found! Evict D.
```

**Why clock-sweep instead of LRU?** A true LRU requires maintaining a doubly-linked list that must be updated on every buffer access — this creates contention in a multi-process system. Clock-sweep approximates LRU with a simple integer counter, which is cheaper to update atomically.

#### Dirty Page Write-Back

Dirty pages (modified in memory but not yet on disk) are written back by:
- **Background Writer (bgwriter):** Periodically writes dirty buffers to disk to ensure free buffers are available. Controlled by `bgwriter_delay` and `bgwriter_lru_maxpages`.
- **Checkpointer:** Periodically writes ALL dirty buffers and creates a checkpoint record in the WAL. This bounds crash recovery time.
- **Backend eviction:** If a backend can't find a clean buffer to evict, it writes the dirty victim page itself (this is slow and indicates `shared_buffers` is too small or bgwriter is too slow).

### 3.2 B-Tree Implementation

**Source location:** `src/backend/access/nbtree/`

PostgreSQL's B-Tree is a **Lehman-Yao style B+ tree** — a variant designed for concurrent access without holding locks across tree levels.

#### Index Page Layout

```
B-Tree Index Page (8 KB):
┌─────────────────────────────────────────────────────┐
│  Page Header (24 bytes)                              │
│  - pd_lsn: Last WAL LSN that modified this page     │
│  - pd_lower / pd_upper: Free space boundaries        │
├─────────────────────────────────────────────────────┤
│  Special Area (BTPageOpaqueData - 16 bytes)         │
│  - btpo_prev: Left sibling page                      │
│  - btpo_next: Right sibling page (Lehman-Yao link)   │
│  - btpo_level: Level in tree (0 = leaf)              │
│  - btpo_flags: BTP_LEAF, BTP_ROOT, BTP_DELETED, etc.│
├─────────────────────────────────────────────────────┤
│  Line Pointers                                       │
│  [ItemId 1] [ItemId 2] [ItemId 3] ... [ItemId N]    │
├─────────────────────────────────────────────────────┤
│  Free Space                                          │
├─────────────────────────────────────────────────────┤
│  Index Tuples (sorted by key)                        │
│  ┌────────────────────────────────────────────┐     │
│  │ Internal page tuple:                       │     │
│  │   Key Value + Child Page Pointer (BlockId) │     │
│  ├────────────────────────────────────────────┤     │
│  │ Leaf page tuple:                           │     │
│  │   Key Value + Heap TID (block + offset)    │     │
│  └────────────────────────────────────────────┘     │
└─────────────────────────────────────────────────────┘
```

#### Search Path

```
Searching for key = 42 in B-Tree:

    Root Page (Level 2)
    ┌─────────────────────────┐
    │  [10] → P1  [30] → P2  │ ─── 42 > 30, follow rightmost pointer
    │  [50] → P3  [∞] → P4   │ ─── 42 < 50, follow P3
    └─────────────┬───────────┘
                  │
    Internal Page P3 (Level 1)
    ┌─────────────┴───────────┐
    │  [31] → L1  [38] → L2  │ ─── 42 > 38, follow rightmost
    │  [45] → L3  [∞] → L4   │ ─── 42 < 45, follow L3
    └─────────────┬───────────┘
                  │
    Leaf Page L3 (Level 0)
    ┌─────────────┴───────────┐
    │  [38, TID(5,2)]         │
    │  [40, TID(12,7)]        │
    │  [42, TID(8,3)]  ← FOUND│    TID = (block 8, offset 3)
    │  [44, TID(3,1)]         │
    │  btpo_next → L4         │ ─── Right-link for range scans
    └─────────────────────────┘
                  │
                  ▼ Heap fetch
    Heap Page 8, Offset 3 → Actual row data
```

#### Insert and Page Splits

When a leaf page is full and a new key must be inserted:

1. The page is **split** into two pages. The median key is copied up to the parent.
2. The right-link pointer (`btpo_next`) connects the two new siblings.
3. **Lehman-Yao optimization:** During concurrent searches, if a search descends to a page and finds the key should be on the right sibling (due to a concurrent split), it follows the right-link. This means **tree traversal only needs to lock one page at a time** (no lock coupling across levels).

```
Page Split Example:

Before (Page P is full):
┌───────────────────────────┐
│ [10] [20] [30] [40] [50]  │  ← Full
└───────────────────────────┘

Insert key 35:

After:
┌──────────────────┐  btpo_next  ┌──────────────────┐
│ [10] [20] [30]   │───────────▶│ [35] [40] [50]   │
└──────────────────┘             └──────────────────┘
              │
              ▼ Key [35] copied up to parent
    Parent page updated with new child pointer
```

**Why Lehman-Yao?** Traditional B-tree algorithms require holding a lock on the parent page while splitting a child. Lehman-Yao's right-link approach allows each page to be locked independently, dramatically improving concurrency for write-heavy index workloads.

### 3.3 MVCC (Multi-Version Concurrency Control)

#### Heap Tuple Versioning

Every tuple in PostgreSQL's heap contains MVCC metadata:

```
HeapTupleHeaderData:
┌─────────────────────────────────────────────┐
│  t_xmin (4 bytes)  │  Transaction ID that   │
│                     │  inserted this tuple    │
├─────────────────────┼────────────────────────┤
│  t_xmax (4 bytes)  │  Transaction ID that   │
│                     │  deleted/updated this   │
│                     │  tuple (0 if alive)     │
├─────────────────────┼────────────────────────┤
│  t_cid (4 bytes)   │  Command ID within the │
│                     │  inserting transaction   │
├─────────────────────┼────────────────────────┤
│  t_ctid (6 bytes)  │  Current TID - points  │
│                     │  to self (if current)   │
│                     │  or to newer version    │
├─────────────────────┼────────────────────────┤
│  t_infomask (2 B)  │  Status flags:          │
│                     │  HEAP_XMIN_COMMITTED    │
│                     │  HEAP_XMIN_INVALID      │
│                     │  HEAP_XMAX_COMMITTED    │
│                     │  HEAP_HASOID, etc.      │
├─────────────────────┼────────────────────────┤
│  t_infomask2 (2 B) │  Number of attributes, │
│                     │  HOT flag, etc.         │
├─────────────────────┼────────────────────────┤
│  t_hoff (1 byte)   │  Offset to user data    │
├─────────────────────┼────────────────────────┤
│  t_bits[]           │  Null bitmap            │
├─────────────────────┼────────────────────────┤
│  User Data          │  Actual column values   │
└─────────────────────┴────────────────────────┘
```

#### Visibility Rules

A tuple is **visible** to a transaction if:

```python
def is_visible(tuple, snapshot):
    # Rule 1: The inserting transaction must be committed
    # and must have committed before our snapshot was taken
    if not is_committed(tuple.xmin):
        # Exception: if WE inserted it (same transaction)
        if tuple.xmin == snapshot.current_xid:
            # But only if it wasn't also deleted by us
            if tuple.xmax == 0 or not is_committed(tuple.xmax):
                return True
        return False
    
    if tuple.xmin in snapshot.active_xids:
        return False  # Inserter was still active when we started
    
    if tuple.xmin > snapshot.xmax:
        return False  # Inserted after our snapshot
    
    # Rule 2: The tuple must NOT have been deleted
    # (or the deleting transaction must not be visible to us)
    if tuple.xmax == 0:
        return True  # Never deleted
    
    if not is_committed(tuple.xmax):
        return True  # Deleter hasn't committed
    
    if tuple.xmax in snapshot.active_xids:
        return True  # Deleter was active when we started
    
    if tuple.xmax > snapshot.xmax:
        return True  # Deleted after our snapshot
    
    return False  # Tuple was deleted and visible to us
```

#### Update Chain Example

```
Initial state - Transaction 100 inserts a row:

  Page 5, Offset 1:
  ┌────────────────────────────────────────┐
  │ xmin=100 | xmax=0 | ctid=(5,1)        │
  │ name="Alice" | age=25                  │
  └────────────────────────────────────────┘

Transaction 200 updates age to 26:

  Page 5, Offset 1 (old version):
  ┌────────────────────────────────────────┐
  │ xmin=100 | xmax=200 | ctid=(5,3)  ────┼──┐  Points to new version
  │ name="Alice" | age=25  [DEAD]          │  │
  └────────────────────────────────────────┘  │
                                               │
  Page 5, Offset 3 (new version):              │
  ┌────────────────────────────────────────┐  │
  │ xmin=200 | xmax=0 | ctid=(5,3)    ◀───┼──┘
  │ name="Alice" | age=26  [LIVE]          │
  └────────────────────────────────────────┘

Transaction 300 updates age to 27:

  Page 5, Offset 1:  xmin=100, xmax=200 → (5,3)  [DEAD]
  Page 5, Offset 3:  xmin=200, xmax=300 → (7,1)  [DEAD]
  Page 7, Offset 1:  xmin=300, xmax=0   → (7,1)  [LIVE]
```

#### Why VACUUM Is Necessary

Dead tuples (where `xmax` is set and the deleting transaction has committed and is no longer visible to ANY active transaction) waste space and slow down scans. **VACUUM** does three things:

1. **Removes dead tuples** and marks their space as reusable in the page's free space map (FSM).
2. **Freezes old tuple xmin values** — replaces the 32-bit transaction ID with a special "frozen" flag, preventing transaction ID wraparound (which would make all data invisible).
3. **Updates the visibility map (VM)** — marks pages where all tuples are visible to all transactions, enabling **index-only scans** (the executor can skip the heap fetch for these pages).

```
Before VACUUM:
Page contains: [LIVE] [DEAD] [DEAD] [LIVE] [DEAD]
Free Space Map: 0 bytes free

After VACUUM:
Page contains: [LIVE] [FREE] [FREE] [LIVE] [FREE]
Free Space Map: 3 * tuple_size bytes free
Visibility Map: NOT all-visible (some tuples still have recent xmin)
```

**Autovacuum** runs this automatically. It monitors `pg_stat_all_tables.n_dead_tup` and triggers when dead tuples exceed `autovacuum_vacuum_threshold + autovacuum_vacuum_scale_factor * n_live_tup`.

### 3.4 WAL (Write-Ahead Logging)

#### Core Principle

**The WAL guarantee:** No data page modification is written to disk until the corresponding WAL record has been flushed to stable storage. This ensures that after a crash, PostgreSQL can reconstruct the state of data pages by replaying WAL records.

#### WAL Record Structure

```
WAL Record:
┌────────────────────────────────────────────────┐
│  XLogRecord Header                              │
│  - xl_tot_len:  Total record length             │
│  - xl_xid:      Transaction ID                  │
│  - xl_prev:     Previous record's LSN           │
│  - xl_info:     Record type + flags              │
│  - xl_rmid:     Resource Manager ID              │
│  │              (HEAP, BTREE, XACT, etc.)       │
│  - xl_crc:      CRC-32C checksum                │
├────────────────────────────────────────────────┤
│  Record-specific data                           │
│  (e.g., for heap insert: offset, tuple data)    │
├────────────────────────────────────────────────┤
│  Backup Block Data (if full-page write)         │
│  (Complete 8KB page image after modification)    │
└────────────────────────────────────────────────┘
```

#### WAL Flow During Transaction

```
Transaction: INSERT INTO users VALUES (1, 'Alice');

  1. Backend acquires buffer containing target heap page
     └─→ Buffer Manager: ReadBuffer() → pins page in shared buffers

  2. Backend generates WAL record for the insert
     └─→ WAL record: {rmgr=HEAP, info=INSERT, data=(offset, tuple)}

  3. WAL record written to WAL buffers (in shared memory)
     └─→ XLogInsert() → returns LSN (Log Sequence Number)

  4. Page header's pd_lsn updated to this LSN
     └─→ This marks the page as "modified after this WAL record"

  5. Page marked dirty in buffer descriptor

  6. On COMMIT:
     └─→ WAL records flushed to disk: XLogFlush(commit_lsn)
     └─→ Only AFTER WAL flush, client receives "COMMIT OK"

  7. Later (asynchronously):
     └─→ Checkpointer or bgwriter writes dirty page to disk
     └─→ Page can only be written AFTER its pd_lsn's WAL record is flushed
```

#### Crash Recovery Process

```
                        Crash occurs here
                              │
  ────────────────────────────▼───────────────────────
  Timeline:
  
  [Checkpoint]──────[WAL Records]──────[CRASH]
       │                  │
       ▼                  ▼
  Redo Point        Records to replay
  
  Recovery steps:
  
  1. Find latest checkpoint record in pg_control
  2. Read the checkpoint's redo point (LSN)
  3. Start reading WAL from the redo point forward
  4. For each WAL record:
     a. Read the data page from disk
     b. Compare page's pd_lsn with the WAL record's LSN
     c. If pd_lsn < WAL record LSN:
        → Page is stale, apply the WAL record (redo)
     d. If pd_lsn >= WAL record LSN:
        → Page already has this change, skip
  5. Continue until end of WAL
  6. Mark recovery complete
```

#### Full-Page Writes (FPW)

After a checkpoint, the **first modification** to any page results in a **full-page write** — the entire 8 KB page image is included in the WAL record. This protects against **torn pages**: if the OS writes only a partial page (e.g., 4 KB of 8 KB) before crashing, the WAL contains a complete page image to restore from.

```
Checkpoint occurs. Then:

First write to Page X after checkpoint:
  WAL Record = {operation data} + {full 8 KB page image}
  Size: ~8200 bytes (large!)

Subsequent writes to Page X (before next checkpoint):
  WAL Record = {operation data only}
  Size: ~100-200 bytes (small)
```

**Trade-off:** Full-page writes increase WAL volume (sometimes 2-3x), but they guarantee recovery correctness even with torn page writes. The `full_page_writes` parameter controls this (default ON; turning it off is unsafe unless the filesystem guarantees atomic page writes).

#### Checkpointing

The checkpointer process periodically:
1. Writes all dirty shared buffers to disk.
2. Writes a checkpoint WAL record containing the redo point.
3. Updates `pg_control` with the checkpoint location.

This bounds recovery time: after a crash, only WAL records since the last checkpoint need to be replayed. Checkpoints are triggered by:
- `checkpoint_timeout` (default 5 minutes)
- `max_wal_size` being approached (default 1 GB)
- Manual `CHECKPOINT` command

### 3.5 Query Planning and pg_statistic

The query planner's effectiveness depends critically on accurate statistics about data distribution.

#### How Statistics Are Collected

```sql
ANALYZE users;   -- Collects statistics for the 'users' table
-- Or: autovacuum does this automatically (ANALYZE phase)
```

`ANALYZE` samples rows (default `default_statistics_target = 100` means ~30,000 rows sampled) and computes:

| Statistic | Stored in | Purpose |
|---|---|---|
| `n_distinct` | `pg_statistic` | Number of distinct values in a column |
| `null_frac` | `pg_statistic` | Fraction of NULL values |
| `avg_width` | `pg_statistic` | Average byte width of column values |
| Most Common Values (MCV) | `pg_statistic.stakind/stavalues` | Top-N most frequent values and their frequencies |
| Histogram bounds | `pg_statistic.stakind/stavalues` | Equal-depth histogram of non-MCV values |
| Correlation | `pg_statistic` | Physical vs. logical ordering correlation |

#### How the Planner Uses Statistics

```sql
EXPLAIN ANALYZE SELECT * FROM orders WHERE status = 'shipped' AND total > 100;

-- Planner reasoning:
-- 1. Check pg_statistic for 'status' column:
--    MCV: {'pending': 0.4, 'shipped': 0.35, 'delivered': 0.2, 'cancelled': 0.05}
--    → selectivity of status='shipped' = 0.35
--
-- 2. Check pg_statistic for 'total' column:
--    Histogram: [0, 25, 50, 75, 100, 150, 200, 500, 1000]
--    → selectivity of total > 100 ≈ 0.5 (rough interpolation)
--
-- 3. Combined selectivity (assuming independence): 0.35 * 0.5 = 0.175
--    → Estimated rows: 0.175 * 100000 = 17,500
--
-- 4. Cost comparison:
--    Sequential scan: read all 100K rows, filter → cost = 1500
--    Index scan on idx_status: 17,500 index lookups + heap fetches → cost = 2000
--    Index scan on idx_total: 50,000 lookups + filter → cost = 3500
--    Bitmap index scan combining both: → cost = 800  ← Winner!
```

---

## 4. Design Trade-Offs

### 4.1 Buffer Manager: Shared Buffers vs. OS Page Cache

| Approach | PostgreSQL Shared Buffers | Relying on OS Page Cache |
|---|---|---|
| **Eviction policy** | Clock-sweep (application-aware) | LRU/ARC (generic) |
| **Dirty page tracking** | Explicit (buffer descriptors) | Implicit (page tables) |
| **Double buffering** | Yes — data exists in both shared buffers AND OS cache | No double buffering |
| **Memory efficiency** | Wastes memory due to double caching | Better memory utilization |
| **Write ordering** | PostgreSQL controls write-back timing | OS may write pages in any order |

**Why PostgreSQL maintains its own cache:** The WAL protocol requires that dirty pages are not written to disk until their WAL records are flushed. The OS page cache cannot guarantee this ordering. PostgreSQL must control when and which pages are written back. Additionally, application-specific knowledge (e.g., sequential scan buffers should be evicted quickly) allows better eviction decisions than a generic OS cache.

**Best practice:** Set `shared_buffers` to ~25% of RAM. The remaining 75% serves as OS page cache, reducing I/O for read-heavy workloads despite the double-buffering inefficiency.

### 4.2 MVCC: Append-Only vs. In-Place Updates

| Aspect | PostgreSQL (Append-only) | In-place (e.g., InnoDB) |
|---|---|---|
| Update mechanism | Create new tuple version | Modify existing row, store old in undo log |
| Dead tuple cleanup | VACUUM (periodic) | Undo log purge (continuous) |
| Read performance during updates | Scans may traverse dead tuples | Clean reads (undo only consulted if needed) |
| Write amplification | Lower for updates (no undo log write) | Higher (write to both data page and undo log) |
| Index maintenance | May require new index entries per update | Index entries remain stable |
| Complexity | Simpler (no undo log management) | More complex (undo chain management) |

**Why PostgreSQL chose append-only:** Historical simplicity. The POSTGRES project predated many modern MVCC techniques. The append-only model is conceptually clean — every version of a row exists as an independent tuple. The downside (VACUUM) was considered acceptable given the benefit of implementation simplicity and crash recovery robustness.

### 4.3 Process-Per-Connection vs. Thread-Per-Connection

| Aspect | PostgreSQL (Process) | Thread-based (e.g., MySQL) |
|---|---|---|
| Memory overhead | ~5-10 MB per connection | ~1-2 MB per thread |
| Context switching | OS process context switch (expensive) | Thread context switch (cheaper) |
| Fault isolation | Backend crash doesn't affect others | Thread crash can bring down server |
| Max connections | Practical limit ~500-1000 | Can handle thousands |
| Shared state | Via shared memory (explicit) | Via shared address space (implicit) |

**Trade-off resolution:** Connection poolers (PgBouncer, pgpool-II) are the standard solution. They maintain a small pool of actual backend connections and multiplex client connections onto them. This turns the per-connection overhead from a practical limitation into a non-issue for most deployments.

---

## 5. Experiments / Observations

### 5.1 EXPLAIN ANALYZE on a Multi-Table Join

```sql
-- Schema:
-- customers(id PK, name, city)        - 100,000 rows
-- orders(id PK, customer_id FK, date) - 500,000 rows  
-- items(id PK, order_id FK, product, price) - 2,000,000 rows

EXPLAIN (ANALYZE, BUFFERS, FORMAT TEXT) 
SELECT c.name, COUNT(*) as order_count, SUM(i.price) as total_spent
FROM customers c
JOIN orders o ON c.id = o.customer_id
JOIN items i ON o.id = i.order_id
WHERE c.city = 'Mumbai'
GROUP BY c.name
ORDER BY total_spent DESC
LIMIT 10;
```

**Typical execution plan:**

```
 Limit  (cost=45000..45000 rows=10 width=48) (actual time=850..850 rows=10 loops=1)
   ->  Sort  (cost=45000..45050 rows=200 width=48) (actual time=850..850 rows=10 loops=1)
         Sort Key: sum(i.price) DESC
         Sort Method: top-N heapsort  Memory: 26kB
         ->  HashAggregate  (cost=44000..44200 rows=200 width=48) (actual time=848..849 rows=187 loops=1)
               Group Key: c.name
               Batches: 1  Memory Usage: 60kB
               ->  Hash Join  (cost=3500..42000 rows=80000 width=22) (actual time=35..780 rows=78500 loops=1)
                     Hash Cond: (i.order_id = o.id)
                     ->  Seq Scan on items i  (cost=0..30000 rows=2000000 width=12) (actual time=0.02..250 rows=2000000 loops=1)
                           Buffers: shared hit=15000 read=3000
                     ->  Hash  (cost=3000..3000 rows=5000 width=18) (actual time=30..30 rows=4800 loops=1)
                           Buckets: 8192  Batches: 1  Memory Usage: 300kB
                           ->  Hash Join  (cost=200..3000 rows=5000 width=18) (actual time=2..28 rows=4800 loops=1)
                                 Hash Cond: (o.customer_id = c.id)
                                 ->  Seq Scan on orders o  (cost=0..2500 rows=500000 width=8) (actual time=0.01..80 rows=500000 loops=1)
                                       Buffers: shared hit=4000
                                 ->  Hash  (cost=180..180 rows=200 width=18) (actual time=1.5..1.5 rows=187 loops=1)
                                       Buckets: 1024  Batches: 1  Memory Usage: 15kB
                                       ->  Index Scan using idx_customers_city on customers c  
                                             (cost=0..180 rows=200 width=18) (actual time=0.05..1.2 rows=187 loops=1)
                                             Index Cond: (city = 'Mumbai'::text)
                                             Buffers: shared hit=5
 Planning Time: 1.2 ms
 Execution Time: 852 ms
```

**Analysis of planner decisions:**

1. **Index Scan on customers.city:** The planner estimated 200 rows for city='Mumbai' using the MCV list from `pg_statistic`. The actual count (187) is close — good statistics.

2. **Hash Join strategy:** With only 187 customers, the planner builds a hash table from the filtered customers (tiny — 15 KB) and probes it with the orders table. This is optimal because the inner relation is small.

3. **Second Hash Join:** The joined customers×orders result (~4800 rows, 300 KB hash table) is then hash-joined with the 2M items table. The planner chose Hash Join over Nested Loop because 4800 probe lookups against a seq scan of items is cheaper than 4800 index lookups.

4. **Buffers analysis:** `shared hit=15000 read=3000` on the items scan tells us that 83% of pages were in shared buffers (good cache hit ratio) but 3000 pages needed disk reads. This suggests the items table is larger than shared_buffers.

5. **Estimate accuracy:** Estimated 200 customers, got 187. Estimated 5000 orders, got 4800. Estimated 80000 items, got 78500. The planner's estimates are within 10% — this is what good statistics look like.

### 5.2 Observing MVCC Behavior

```sql
-- Session 1:
BEGIN;
SELECT xmin, xmax, ctid, * FROM users WHERE id = 1;
--  xmin  | xmax | ctid  | id | name
-- -------+------+-------+----+-------
--  1000  |  0   | (0,1) |  1 | Alice

UPDATE users SET name = 'Bob' WHERE id = 1;

SELECT xmin, xmax, ctid, * FROM users WHERE id = 1;
--  xmin  | xmax | ctid  | id | name
-- -------+------+-------+----+-------
--  1050  |  0   | (0,5) |  1 | Bob
-- Note: new xmin (our txid), new ctid (new physical location)

-- The old tuple at (0,1) now has xmax=1050, ctid=(0,5)
```

### 5.3 WAL Generation Measurement

```sql
SELECT pg_current_wal_lsn();  -- Before: 0/1A000060

INSERT INTO test SELECT generate_series(1, 100000), 'test data';

SELECT pg_current_wal_lsn();  -- After: 0/1C500000
-- WAL generated: ~38 MB for 100K inserts

SELECT pg_current_wal_lsn();  -- Before update
UPDATE test SET data = 'updated' WHERE id <= 50000;
SELECT pg_current_wal_lsn();  -- After update
-- WAL generated: ~50 MB (more than insert due to full-page writes)
```

**Observation:** Updates generate more WAL than inserts because: (1) the first modification to each page after a checkpoint triggers a full-page write (8 KB per page), and (2) both the old tuple invalidation and new tuple creation generate WAL records.

---

## 6. Key Learnings

### Architectural Insights

1. **The Buffer Manager is the central nervous system.** Every data access flows through it. Understanding clock-sweep, pinning, and dirty page management explains most PostgreSQL performance behaviors. If `shared_buffers` is too small, backends spend time evicting dirty pages instead of processing queries.

2. **MVCC is elegant but has operational costs.** The append-only model means updates always create new tuples. Without VACUUM, tables grow without bound ("table bloat"). The HOT (Heap-Only Tuple) optimization partially mitigates this — if the updated columns aren't indexed and there's room on the same page, the new tuple can be placed on the same page without updating any index.

3. **WAL is the foundation of durability AND replication.** The same WAL stream that enables crash recovery also enables streaming replication. A replica simply reads and replays WAL records from the primary. This architectural reuse is a brilliant example of design economy.

4. **The planner is only as good as its statistics.** Stale `pg_statistic` data leads to bad plans — for example, using a sequential scan when an index scan would be 100x faster, or vice versa. Running `ANALYZE` (or letting autovacuum do it) is critical for query performance.

5. **Lehman-Yao B-trees solve a real concurrency problem.** Standard B-tree implementations require "lock coupling" (holding a parent lock while accessing a child), which creates contention. The right-link approach lets concurrent readers and writers operate with minimal lock contention, which is essential for a multi-process database.

### Surprising Observations

- **VACUUM FULL rewrites the entire table.** Regular VACUUM only marks space as reusable within existing pages; it doesn't shrink the file. To actually return space to the OS, you need `VACUUM FULL`, which acquires an exclusive lock and rewrites the table — essentially an offline operation.
- **Index-only scans depend on the visibility map.** Even if all needed columns are in the index, PostgreSQL must check the heap to verify tuple visibility — unless the visibility map confirms all tuples on that page are visible. This is why VACUUM's visibility map updates have a direct impact on query performance.
- **The 32-bit transaction ID limit is a real operational concern.** PostgreSQL's transaction IDs are 32-bit (~4 billion values). Without regular VACUUM to "freeze" old tuples, the database can enter "transaction ID wraparound" protection mode, which forces a VACUUM FREEZE that stops all other operations. In production, monitoring `age(datfrozenxid)` is essential.

### Summary Table

| Component | Key Mechanism | Why It Matters |
|---|---|---|
| Buffer Manager | Clock-sweep + dirty tracking | Controls all I/O; misconfigs cause cache thrashing |
| B-Tree | Lehman-Yao concurrent B+ tree | Enables high concurrency without lock coupling |
| MVCC | xmin/xmax tuple versioning | Readers never block writers, but VACUUM is required |
| WAL | Write-ahead logging with FPW | Guarantees durability; enables replication |
| Planner | Cost-based with pg_statistic | Good stats = good plans; stale stats = bad plans |

---

## References

1. PostgreSQL Documentation — Internals: [https://www.postgresql.org/docs/current/internals.html](https://www.postgresql.org/docs/current/internals.html)
2. PostgreSQL Source Code: `src/backend/storage/buffer/`, `src/backend/access/nbtree/`, `src/backend/access/heap/`
3. Hellerstein, J.M., Stonebraker, M., Hamilton, J. (2007). "Architecture of a Database System." Foundations and Trends in Databases.
4. Lehman, P.L., Yao, S.B. (1981). "Efficient Locking for Concurrent Operations on B-Trees." ACM TODS.
5. Momjian, B. (2001). "PostgreSQL: Introduction and Concepts." Addison-Wesley.
6. Suzuki, H. "The Internals of PostgreSQL." [https://www.interdb.jp/pg/](https://www.interdb.jp/pg/)
7. PostgreSQL Wiki — MVCC: [https://wiki.postgresql.org/wiki/MVCC](https://wiki.postgresql.org/wiki/MVCC)
