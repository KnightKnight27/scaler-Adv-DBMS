# PostgreSQL Internal Architecture

## 1. Problem Background

PostgreSQL is an advanced open-source object-relational database management system that has been under active development for over 35 years. It began as the POSTGRES project at UC Berkeley (1986) under Michael Stonebraker, aiming to explore next-generation database concepts like extensible type systems, rule-based query rewriting, and multi-version concurrency control.

### Why Study PostgreSQL Internals?

PostgreSQL's internal architecture represents a masterclass in **engineering trade-offs**. Every component — from the buffer manager to the WAL subsystem — reflects deliberate choices that balance:
- **Performance vs. correctness** (e.g., MVCC sacrifices storage space for non-blocking reads)
- **Simplicity vs. capability** (e.g., process-per-connection vs. thread pools)
- **Write throughput vs. durability** (e.g., WAL fsync settings)

Understanding these internals helps in:
- Diagnosing performance problems (why is a query slow? Is it a buffer miss? Bad plan? Lock contention?)
- Tuning configuration parameters with informed reasoning
- Appreciating why features like VACUUM exist and when autovacuum needs manual intervention

---

## 2. Architecture Overview

### High-Level Component Architecture

```
                        Client Connections
                              │
                    ┌─────────▼─────────┐
                    │    Postmaster      │
                    │  (Main Process)    │
                    └─────────┬─────────┘
                              │ fork()
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
      ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
      │   Backend 1  │ │   Backend 2  │ │   Backend N  │
      │              │ │              │ │              │
      │ ┌──────────┐ │ │ ┌──────────┐ │ │ ┌──────────┐ │
      │ │  Parser  │ │ │ │  Parser  │ │ │ │  Parser  │ │
      │ ├──────────┤ │ │ ├──────────┤ │ │ ├──────────┤ │
      │ │ Analyzer │ │ │ │ Analyzer │ │ │ │ Analyzer │ │
      │ ├──────────┤ │ │ ├──────────┤ │ │ ├──────────┤ │
      │ │ Planner/ │ │ │ │ Planner/ │ │ │ │ Planner/ │ │
      │ │ Optimizer│ │ │ │ Optimizer│ │ │ │ Optimizer│ │
      │ ├──────────┤ │ │ ├──────────┤ │ │ ├──────────┤ │
      │ │ Executor │ │ │ │ Executor │ │ │ │ Executor │ │
      │ └────┬─────┘ │ │ └────┬─────┘ │ │ └────┬─────┘ │
      └──────┼───────┘ └──────┼───────┘ └──────┼───────┘
             │                │                │
    ┌────────▼────────────────▼────────────────▼────────┐
    │               Shared Memory                       │
    │  ┌─────────────────────────────────────────────┐  │
    │  │            Shared Buffer Pool               │  │
    │  │     (shared_buffers: typically 25% RAM)      │  │
    │  └─────────────────────────────────────────────┘  │
    │  ┌───────────────┐  ┌───────────────────────┐     │
    │  │  WAL Buffers  │  │  Lock Manager         │     │
    │  │  (wal_buffers)│  │  (LWLocks + HWLocks)  │     │
    │  └───────────────┘  └───────────────────────┘     │
    │  ┌───────────────┐  ┌───────────────────────┐     │
    │  │  CLOG Buffers │  │  Proc Array           │     │
    │  │  (txn status) │  │  (active txn list)    │     │
    │  └───────────────┘  └───────────────────────┘     │
    └───────────────────────┬───────────────────────────┘
                            │
    ┌───────────────────────▼───────────────────────────┐
    │               Background Processes                 │
    │  ┌──────────┐ ┌───────────┐ ┌──────────────────┐  │
    │  │BGWriter  │ │Checkpointr│ │   WAL Writer     │  │
    │  │(dirty pg │ │(periodic  │ │   (flush WAL     │  │
    │  │ writes)  │ │ flush)    │ │    to disk)      │  │
    │  └──────────┘ └───────────┘ └──────────────────┘  │
    │  ┌──────────┐ ┌───────────┐ ┌──────────────────┐  │
    │  │Autovacuum│ │Stats      │ │   WAL Archiver   │  │
    │  │Launcher  │ │Collector  │ │   (for PITR)     │  │
    │  └──────────┘ └───────────┘ └──────────────────┘  │
    └───────────────────────┬───────────────────────────┘
                            │
    ┌───────────────────────▼───────────────────────────┐
    │                  Disk Storage                      │
    │  base/           pg_wal/          pg_xact/         │
    │  (data files)    (WAL segments)   (commit log)     │
    │                                                    │
    │  pg_stat/        pg_tblspc/      pg_multixact/    │
    │  (statistics)    (tablespaces)   (multi-txn)       │
    └───────────────────────────────────────────────────┘
```

### Query Processing Pipeline

```
SQL Text: "SELECT * FROM users WHERE age > 30"
                    │
                    ▼
            ┌──────────────┐
            │    Parser    │     Lexer + Grammar (Bison/Flex)
            │              │     Produces: Raw Parse Tree
            └──────┬───────┘
                   ▼
            ┌──────────────┐
            │   Analyzer   │     Semantic analysis, name resolution
            │  (Rewriter)  │     Produces: Query Tree
            └──────┬───────┘     Applies: View expansion, rules
                   ▼
            ┌──────────────┐
            │   Planner /  │     Cost-based optimization
            │   Optimizer  │     Considers: Join order, scan type,
            └──────┬───────┘     indexes, statistics (pg_statistic)
                   ▼             Produces: Plan Tree
            ┌──────────────┐
            │   Executor   │     Volcano/Iterator model
            │              │     Each node: Init → GetNext → End
            └──────┬───────┘     Produces: Result tuples
                   ▼
              Result Set
```

---

## 3. Internal Design

### 3.1 Buffer Manager (`src/backend/storage/buffer/`)

The Buffer Manager is PostgreSQL's **central caching layer** — it manages the shared buffer pool that sits between the executor and disk. Every page read or written goes through the buffer manager.

#### Architecture

```
Backend Process requests page (relation, block number)
                    │
                    ▼
        ┌───────────────────────┐
        │   Buffer Manager API  │
        │   ReadBuffer()        │
        │   ReadBufferExtended()│
        └───────────┬───────────┘
                    │
                    ▼
        ┌───────────────────────┐     ┌──────────────────┐
        │  Buffer Hash Table   │────▶│  Buffer Pool     │
        │  (tag → buffer_id)   │     │  (shared_buffers) │
        │                      │     │                   │
        │  Tag = {             │     │  Buffer 0: [page] │
        │    RelFileNode,      │     │  Buffer 1: [page] │
        │    ForkNumber,       │     │  Buffer 2: [page] │
        │    BlockNumber       │     │  ...              │
        │  }                   │     │  Buffer N: [page] │
        └──────────────────────┘     └────────┬──────────┘
                                              │
                                     ┌────────▼──────────┐
                                     │ Buffer Descriptors│
                                     │ (per-buffer state) │
                                     │ - tag              │
                                     │ - flags (dirty,    │
                                     │   valid, locked)   │
                                     │ - usage_count      │
                                     │ - refcount         │
                                     │ - content_lock     │
                                     └───────────────────┘
```

#### Page Caching: How It Works

When a backend process needs to read a page:

1. **Hash lookup**: Compute the buffer tag `(RelFileNode, ForkNum, BlockNum)` and look it up in the shared hash table
2. **Cache hit**: If found, increment `usage_count` and `refcount`, return a pointer to the buffer
3. **Cache miss**: 
   - Find a victim buffer using the **clock-sweep** algorithm
   - If the victim buffer is **dirty**, write it to disk first (or rely on the background writer having already done so)
   - Read the new page from disk into the victim buffer's slot
   - Insert the new tag into the hash table

#### Buffer Replacement: Clock-Sweep Algorithm

PostgreSQL uses a **clock-sweep** algorithm (variant of the clock / second-chance algorithm), NOT LRU. This is an important architectural decision.

```
Clock-Sweep Visualization:

    Buffer Pool: [B0] [B1] [B2] [B3] [B4] [B5] [B6] [B7]
    Usage Count:  3    1    0    5    2    0    1    4
    
    Clock hand sweeps through buffers:
                         ↓ (clock hand position)
    
    Step 1: B2 has usage_count=0 → VICTIM FOUND
            (If B2 is dirty, flush first, then replace)
    
    If usage_count > 0:
        Decrement usage_count by 1
        Advance clock hand
    
    If usage_count == 0 AND refcount == 0:
        This buffer is the victim
```

**Why clock-sweep instead of LRU?**
- LRU requires maintaining a linked list and updating it on every access — expensive under high concurrency (lock contention)
- Clock-sweep only updates a counter (atomic operation) on access and does the sweep only when finding a victim
- The `usage_count` acts as a "popularity" metric — frequently accessed pages survive multiple sweeps

**The `shared_buffers` parameter**: This controls how many 8KB page slots exist in the buffer pool. The recommended setting is **25% of available RAM**. Setting it too high wastes memory (the OS also caches files). Setting it too low causes excessive disk I/O.

#### Background Writer and Checkpointer

Two processes help manage dirty buffers:

**Background Writer (`bgwriter`)**: Periodically scans the buffer pool and writes dirty pages that have low usage counts. The goal is to ensure clean buffers are available when backends need to evict a page, avoiding synchronous writes in the critical path.

**Checkpointer**: Periodically performs a **checkpoint** — flushing ALL dirty buffers and writing a checkpoint record to WAL. This bounds the amount of WAL that must be replayed during crash recovery.

```
Time ─────────────────────────────────────────────────────►

    Checkpoint          Checkpoint          Checkpoint
        │                   │                   │
        ▼                   ▼                   ▼
   [flush all dirty]   [flush all dirty]   [flush all dirty]
   [write ckpt record] [write ckpt record] [write ckpt record]

   Recovery only needs to replay WAL from the LAST checkpoint
```

---

### 3.2 B-Tree Implementation (`src/backend/access/nbtree/`)

PostgreSQL's B-Tree is a **Lehman-Yao style B+Tree** with right-links, designed for concurrent access without holding locks on the entire tree.

#### Index Page Layout

```
B-Tree Index Page (8KB):
┌──────────────────────────────────────────────┐
│  PageHeaderData (24 bytes)                   │
│  - pd_lsn (for WAL recovery)                │
│  - pd_flags                                  │
│  - pd_lower, pd_upper, pd_special            │
├──────────────────────────────────────────────┤
│  Line Pointers                               │
│  [lp1] [lp2] [lp3] ... [lpN]                │
├──────────────────────────────────────────────┤
│             Free Space                       │
├──────────────────────────────────────────────┤
│  Index Tuples (from bottom)                  │
│  ┌────────────────────────────────────────┐  │
│  │ IndexTupleData                        │  │
│  │  - t_tid  (heap TID: block + offset)  │  │
│  │  - t_info (size + flags)              │  │
│  │  - key data (column values)           │  │
│  └────────────────────────────────────────┘  │
├──────────────────────────────────────────────┤
│  Special Space (BTPageOpaqueData)            │
│  - btpo_prev  (left sibling)                 │
│  - btpo_next  (right sibling / right-link)   │
│  - btpo_level (0 = leaf)                     │
│  - btpo_flags (leaf, root, deleted, etc.)    │
└──────────────────────────────────────────────┘
```

#### Search Path

```
Searching for key = 42:

              ┌──────────────────┐
              │    Root Page     │
              │  [10] [30] [60]  │
              └───────┬──────────┘
                      │ 30 ≤ 42 < 60
                      ▼
              ┌──────────────────┐
              │  Internal Page   │
              │  [30] [35] [45]  │
              └───────┬──────────┘
                      │ 35 ≤ 42 < 45
                      ▼
    ┌──────────────────────────────────────────┐
    │           Leaf Page                      │
    │  [35,TID1] [38,TID2] [42,TID3] [44,TID4]│
    │         btpo_next ──────────────────► ... │
    └──────────────────────────────────────────┘
                      │
                      ▼
            TID3 = (block=5, offset=3)
                      │
                      ▼  Heap Fetch
            ┌─────────────────┐
            │  Heap Page 5    │
            │  offset 3 →     │
            │  actual row data│
            └─────────────────┘
```

#### Page Splits (Critical Concurrency Challenge)

When a leaf page is full and a new key must be inserted:

```
Before Split:
┌──────────────────────────────────────┐
│ Leaf Page P (FULL)                   │
│ [10] [20] [30] [40] [50] [60]        │
│                     btpo_next → Q    │
└──────────────────────────────────────┘

After Split:
┌──────────────────────┐  ┌──────────────────────┐
│ Leaf Page P          │  │ New Page P'           │
│ [10] [20] [30]       │  │ [40] [50] [60]        │
│        btpo_next → P'│  │        btpo_next → Q  │
└──────────────────────┘  └──────────────────────┘

Then: Insert downlink for P' into the parent page
      (may trigger cascading splits upward)
```

**Lehman-Yao right-links** are critical here. If a concurrent reader descends to page P looking for key=50 just after the split, it won't find 50 on page P. But the right-link tells it to check the right sibling P', where 50 now lives. This avoids holding read locks across the entire root-to-leaf path.

---

### 3.3 MVCC (Multi-Version Concurrency Control)

MVCC is the heart of PostgreSQL's concurrency model. It enables **readers to never block writers, and writers to never block readers**.

#### Heap Tuple Versioning

Every row in PostgreSQL carries **version metadata** in its header:

```
HeapTupleHeader:
┌──────────────────────────────────────────────┐
│  t_xmin  (TransactionId)                     │  ← Transaction that INSERTED this tuple
│  t_xmax  (TransactionId)                     │  ← Transaction that DELETED/UPDATED this tuple
│  t_cid   (CommandId)                         │  ← Command within the transaction
│  t_ctid  (ItemPointerData)                   │  ← Pointer to next version (for HOT chains)
│  t_infomask (uint16)                         │  ← Status bits (committed, aborted, etc.)
│  t_infomask2 (uint16)                        │  ← More flags + number of attributes
│  t_hoff  (uint8)                             │  ← Offset to user data
└──────────────────────────────────────────────┘
│  Null bitmap (optional)                      │
│  User data (actual column values)            │
└──────────────────────────────────────────────┘
```

#### How UPDATE Creates New Versions

```
Transaction 200 executes: UPDATE users SET name = 'Bob' WHERE id = 1;

Step 1: Find the current tuple for id=1

    Heap Page:
    ┌─────────────────────────────────────┐
    │ Tuple A (version 1):               │
    │   t_xmin = 100 (committed)         │
    │   t_xmax = 0   (not deleted)       │
    │   data: {id=1, name='Alice'}       │
    └─────────────────────────────────────┘

Step 2: Mark old tuple as "deleted by txn 200"
         Insert new tuple version

    Heap Page (same or different page):
    ┌─────────────────────────────────────┐
    │ Tuple A (version 1) — DEAD:        │
    │   t_xmin = 100                     │
    │   t_xmax = 200  ← NOW SET          │
    │   t_ctid → Tuple B                 │
    │   data: {id=1, name='Alice'}       │
    ├─────────────────────────────────────┤
    │ Tuple B (version 2) — LIVE:        │
    │   t_xmin = 200                     │
    │   t_xmax = 0                       │
    │   data: {id=1, name='Bob'}         │
    └─────────────────────────────────────┘
```

#### Visibility Rules (Snapshot Isolation)

Each transaction gets a **snapshot** containing:
- `xmin`: The lowest active transaction ID at snapshot time (all txns below this are committed or aborted)
- `xmax`: The next transaction ID to be assigned
- `xip[]`: List of in-progress transaction IDs

**Visibility check for a tuple:**

```
Is tuple visible to snapshot S?

1. Check t_xmin:
   ├── If t_xmin is ABORTED → NOT VISIBLE (tuple was never committed)
   ├── If t_xmin is IN-PROGRESS in S.xip[] → NOT VISIBLE (inserter hasn't committed)
   ├── If t_xmin >= S.xmax → NOT VISIBLE (inserted after snapshot)
   └── If t_xmin is COMMITTED and < S.xmax and NOT in S.xip[] → POSSIBLY VISIBLE
   
2. Check t_xmax (only if step 1 says possibly visible):
   ├── If t_xmax == 0 → VISIBLE (tuple not deleted)
   ├── If t_xmax is ABORTED → VISIBLE (deleter rolled back)
   ├── If t_xmax is IN-PROGRESS in S.xip[] → VISIBLE (deleter hasn't committed yet)
   ├── If t_xmax >= S.xmax → VISIBLE (deleted after our snapshot)
   └── If t_xmax is COMMITTED and < S.xmax → NOT VISIBLE (tuple deleted before snapshot)
```

#### Why VACUUM Is Necessary

```
After many updates, the heap accumulates dead tuples:

Heap Page:
┌─────────────────────────────────────────────┐
│ [DEAD] Tuple v1  (xmax=200, committed)     │ ← Wasted space
│ [DEAD] Tuple v2  (xmax=300, committed)     │ ← Wasted space
│ [DEAD] Tuple v3  (xmax=400, committed)     │ ← Wasted space
│ [LIVE] Tuple v4  (xmax=0)                  │ ← Current version
└─────────────────────────────────────────────┘

VACUUM reclaims space:
┌─────────────────────────────────────────────┐
│ [LIVE] Tuple v4  (xmax=0)                  │
│              Free Space (reclaimed)          │
└─────────────────────────────────────────────┘
```

**VACUUM responsibilities:**
1. Remove dead tuples from heap pages (mark space as free for reuse)
2. Remove index entries pointing to dead tuples
3. Update the **visibility map** (tracks pages with only visible tuples — enables index-only scans)
4. Update the **free space map** (tracks available space for new inserts)
5. Freeze old transaction IDs to prevent **XID wraparound** (critical — PostgreSQL uses 32-bit XIDs that wrap around at ~4 billion)

**Autovacuum** runs automatically but can fall behind under heavy UPDATE/DELETE workloads, leading to **table bloat** — a well-known PostgreSQL operational challenge.

---

### 3.4 WAL (Write-Ahead Logging)

#### The Fundamental Guarantee

> **WAL Rule**: Before any data page modification is flushed to disk, the corresponding WAL record MUST be flushed to stable storage first.

This simple rule is what provides **durability** and **atomicity**. If the system crashes:
- Data pages on disk may be in an inconsistent state (partial writes)
- But the WAL contains a complete record of all changes
- Recovery replays WAL from the last checkpoint to restore consistency

#### WAL Record Structure

```
WAL Record:
┌──────────────────────────────────────────────┐
│  xl_tot_len    (total record length)         │
│  xl_xid        (transaction ID)              │
│  xl_prev       (LSN of previous record)      │
│  xl_info       (operation type: INSERT,       │
│                 UPDATE, DELETE, etc.)         │
│  xl_rmid       (resource manager ID:          │
│                 heap, btree, xact, etc.)      │
│  xl_crc        (CRC checksum)                │
├──────────────────────────────────────────────┤
│  Record-specific data:                       │
│  - For HEAP INSERT: tuple data               │
│  - For HEAP UPDATE: old+new tuple data       │
│  - For BTREE INSERT: key + page info         │
│  - For XACT COMMIT: timestamp, subxacts      │
└──────────────────────────────────────────────┘
```

#### WAL Data Flow

```
Transaction commits:
1. Backend writes WAL record to WAL buffer (shared memory)
2. Backend calls XLogFlush() — forces WAL buffer to disk
3. Transaction is now durable (even if data pages haven't been written)

                    ┌──────────────────┐
                    │  Backend Process  │
                    │                  │
                    │  INSERT INTO ... │
                    └────────┬─────────┘
                             │ 1. Write WAL record
                             ▼
                    ┌──────────────────┐
                    │   WAL Buffer     │      (in shared memory)
                    │   (wal_buffers)  │
                    └────────┬─────────┘
                             │ 2. fsync on COMMIT
                             ▼
                    ┌──────────────────┐
                    │  WAL Segment     │      pg_wal/000000010000000000000001
                    │  Files (16MB)    │      (sequential writes — fast!)
                    └──────────────────┘
                    
Meanwhile, data pages are flushed lazily:
                    ┌──────────────────┐
                    │  Shared Buffer   │ ──── BGWriter/Checkpointer ────►  Data Files
                    │  Pool            │      (asynchronous)               (base/)
                    └──────────────────┘
```

#### Crash Recovery Process

```
System crashes → Restart → PostgreSQL startup:

1. Read pg_control file → find last checkpoint location
2. Open WAL from checkpoint's redo position
3. For each WAL record:
   a. Read the target data page
   b. Check page LSN vs. WAL record LSN
   c. If page LSN < WAL record LSN → apply the change (redo)
   d. If page LSN >= WAL record LSN → skip (already applied)
4. After all WAL records are replayed → database is consistent
5. Start accepting connections

Timeline:
    ─────────────────────────────────────────────────►
    
    Last Checkpoint    Writes...   CRASH    Recovery
         │                          │          │
         │◄── REDO from here ──────►│          │
         │                          │          │
         ▼                          ▼          ▼
    [ckpt record]  [wal records]  [crash]  [replay WAL]
```

#### Checkpointing

A checkpoint ensures that all dirty buffer pages up to a certain WAL position are flushed to disk:

```
checkpoint_timeout (default: 5 min)  or  max_wal_size triggers

Checkpoint process:
1. Record checkpoint start position in WAL
2. Flush all dirty pages from buffer pool to disk
3. Write checkpoint completion record to WAL
4. Update pg_control with new checkpoint location
5. Remove old WAL segments (no longer needed for recovery)
```

**Trade-off**: Frequent checkpoints mean faster recovery (less WAL to replay) but more I/O (flushing dirty pages). Infrequent checkpoints mean faster normal operation but longer recovery times.

---

### 3.5 Query Planning and Statistics

#### How the Planner Works

PostgreSQL's query planner is a **cost-based optimizer** that evaluates multiple possible execution plans and chooses the one with the lowest estimated cost.

```
Planner Decision Tree for: SELECT * FROM A JOIN B ON A.id = B.a_id WHERE A.x > 10

                    ┌─────────────────────────┐
                    │    Enumerate Plans       │
                    └────────────┬────────────┘
                                 │
          ┌──────────────────────┼──────────────────────┐
          ▼                      ▼                      ▼
   ┌─────────────┐       ┌─────────────┐       ┌─────────────┐
   │  Join Order │       │  Join Order │       │  Join Order │
   │  A ⋈ B     │       │  B ⋈ A     │       │  ...        │
   └──────┬──────┘       └──────┬──────┘       └─────────────┘
          │                      │
    ┌─────┼──────┐         ┌─────┼──────┐
    ▼     ▼      ▼         ▼     ▼      ▼
  Hash  Merge  Nested    Hash  Merge  Nested
  Join  Join   Loop      Join  Join   Loop
    │
    ├── Scan A: SeqScan vs IndexScan vs BitmapScan
    ├── Scan B: SeqScan vs IndexScan vs BitmapScan
    └── Cost estimate for each combination
```

#### The `pg_statistic` Catalog

The planner's estimates depend heavily on **table statistics** stored in `pg_statistic` (readable through `pg_stats` view). The `ANALYZE` command collects these statistics.

```sql
-- What the planner knows about a column:
SELECT 
    attname,
    n_distinct,       -- estimated number of distinct values
    null_frac,        -- fraction of NULLs
    avg_width,        -- average column width in bytes
    correlation,      -- physical vs. logical ordering (-1 to +1)
    most_common_vals,  -- most common values
    most_common_freqs, -- frequencies of most common values
    histogram_bounds   -- boundaries for equal-frequency histogram
FROM pg_stats 
WHERE tablename = 'orders';
```

**Why correlation matters**: If `correlation ≈ 1.0`, the column values are in physical order on disk. An index range scan will read pages sequentially (efficient). If `correlation ≈ 0`, values are scattered — a bitmap scan or sequential scan might be cheaper.

---

## 4. Design Trade-Offs

### Process-per-Connection vs. Thread Pool

| Aspect | PostgreSQL (Process) | Alternative (Thread Pool) |
|---|---|---|
| Isolation | Crash in one backend doesn't affect others | Crash can take down entire server |
| Memory | ~5-10MB per connection overhead | Shared address space, lower overhead |
| Context switching | OS process context switch (heavier) | Thread context switch (lighter) |
| Connection limit | Practical limit ~few hundred | Can handle thousands |
| Solution | Use PgBouncer for connection pooling | Built-in (e.g., MySQL thread pool) |

**PostgreSQL's rationale**: Safety and simplicity. Process isolation was chosen when PostgreSQL was designed in the 1990s, and the model has proven robust. Connection pooling solves the scaling problem externally.

### Append-Only Heap vs. In-Place Update

| Aspect | PostgreSQL (Append-Only) | Alternative (In-Place + Undo) |
|---|---|---|
| UPDATE cost | Creates new tuple + marks old as dead | Modifies in place, writes undo record |
| MVCC implementation | Multiple physical tuple versions | Single physical version + undo chain |
| Read performance | May read dead tuples during scan | Only live tuples on data pages |
| Maintenance | Requires VACUUM | No VACUUM equivalent needed |
| Complexity | Simpler implementation | Undo management is complex |
| Crash recovery | Straightforward (WAL replay) | Must handle undo log recovery too |

### WAL Configuration Trade-Offs

| Setting | Conservative | Aggressive |
|---|---|---|
| `synchronous_commit` | `on` (fsync every commit) | `off` (buffer and batch) |
| **Durability** | Guaranteed | May lose last few ms of commits |
| **Performance** | Slower commits | 2-3x faster commits |
| `wal_level` | `replica` (enables replication) | `minimal` (less WAL volume) |
| `checkpoint_timeout` | `5min` (faster recovery) | `30min` (less I/O) |

---

## 5. Experiments / Observations

### Experiment: Analyzing a Multi-Table Join Query

```sql
-- Create test schema
CREATE TABLE customers (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    region VARCHAR(50),
    created_at TIMESTAMP DEFAULT now()
);

CREATE TABLE products (
    id SERIAL PRIMARY KEY,
    product_name VARCHAR(100),
    category VARCHAR(50),
    price NUMERIC(10,2)
);

CREATE TABLE orders (
    id SERIAL PRIMARY KEY,
    customer_id INTEGER REFERENCES customers(id),
    product_id INTEGER REFERENCES products(id),
    quantity INTEGER,
    order_date DATE,
    total NUMERIC(10,2)
);

-- Insert test data
INSERT INTO customers (name, region) 
SELECT 'Customer_' || i, (ARRAY['US','EU','APAC','LATAM'])[1 + (i % 4)]
FROM generate_series(1, 100000) AS i;

INSERT INTO products (product_name, category, price)
SELECT 'Product_' || i, (ARRAY['Electronics','Books','Clothing','Food'])[1 + (i % 4)], 
       (random() * 1000)::numeric(10,2)
FROM generate_series(1, 10000) AS i;

INSERT INTO orders (customer_id, product_id, quantity, order_date, total)
SELECT (random() * 99999 + 1)::int, (random() * 9999 + 1)::int,
       (random() * 10 + 1)::int, 
       '2023-01-01'::date + (random() * 730)::int,
       (random() * 5000)::numeric(10,2)
FROM generate_series(1, 1000000) AS i;

-- Create indexes
CREATE INDEX idx_orders_date ON orders(order_date);
CREATE INDEX idx_orders_customer ON orders(customer_id);
CREATE INDEX idx_customers_region ON customers(region);

-- Gather statistics
ANALYZE;
```

```sql
-- Run the query with EXPLAIN ANALYZE
EXPLAIN (ANALYZE, BUFFERS, FORMAT TEXT) 
SELECT c.region, p.category, 
       COUNT(*) as order_count, 
       SUM(o.total) as revenue
FROM orders o
JOIN customers c ON o.customer_id = c.id
JOIN products p ON o.product_id = p.id
WHERE o.order_date BETWEEN '2024-01-01' AND '2024-06-30'
GROUP BY c.region, p.category
ORDER BY revenue DESC;
```

**Expected Output Analysis:**

```
Sort  (cost=... rows=16 actual time=...) 
  Sort Key: sum(o.total) DESC
  Sort Method: quicksort  Memory: 25kB
  ->  HashAggregate  (cost=... rows=16 actual time=...)
        Group Key: c.region, p.category
        ->  Hash Join  (cost=... rows=... actual time=...)
              Hash Cond: (o.product_id = p.id)
              ->  Hash Join  (cost=... rows=... actual time=...)
                    Hash Cond: (o.customer_id = c.id)
                    ->  Bitmap Heap Scan on orders o  (cost=... actual time=...)
                          Recheck Cond: (order_date >= '2024-01-01' AND order_date <= '2024-06-30')
                          Heap Blocks: exact=...
                          ->  Bitmap Index Scan on idx_orders_date
                                Index Cond: (order_date >= '2024-01-01' AND order_date <= '2024-06-30')
                    ->  Hash  (cost=... rows=100000 actual time=...)
                          Buckets: ...  Batches: 1  Memory Usage: ...
                          ->  Seq Scan on customers c
              ->  Hash  (cost=... rows=10000 actual time=...)
                    Buckets: ...  Batches: 1  Memory Usage: ...
                    ->  Seq Scan on products p
Planning Time: X ms
Execution Time: Y ms
```

**Key observations to analyze:**

1. **Bitmap Index Scan on orders**: The planner chose a bitmap scan rather than a plain index scan for the date range. This is because the range covers ~25% of rows — too many for index scan (random I/O), but a bitmap groups them by page for sequential I/O.

2. **Hash Joins**: For equi-joins with large result sets, Hash Join is typically faster than Nested Loop. The planner builds a hash table from the smaller table (products: 10K rows) and probes it with the larger table.

3. **Sequential Scan on customers**: Even though `idx_customers_region` exists, the planner doesn't use it here — because we need ALL customers (no WHERE filter on region), and a sequential scan of the entire table is faster than an index scan that visits every row.

4. **Planner estimates vs actuals**: Compare `rows=X` (estimated) with `actual rows=Y`. Large discrepancies indicate stale statistics (`ANALYZE` needed) or complex correlations the planner can't model.

5. **Buffers information**: Shows shared buffer hits vs. reads — revealing cache effectiveness. High read counts suggest the working set exceeds `shared_buffers`.

---

## 6. Key Learnings

### 1. The Buffer Manager Is the Performance Bottleneck Gatekeeper
Every query ultimately depends on the buffer manager. Understanding clock-sweep, dirty page management, and the relationship between `shared_buffers`, OS page cache, and disk I/O is essential for performance tuning. The choice of clock-sweep over LRU reflects a system-wide priority: **reduce lock contention at the cost of slightly less optimal eviction**.

### 2. MVCC's Elegance Comes with Maintenance Cost
PostgreSQL's MVCC is conceptually elegant — each transaction sees a consistent snapshot without ever blocking. But the append-only tuple versioning means dead tuples accumulate, requiring VACUUM. This is not a bug but a **fundamental trade-off**: simpler concurrency (no undo log management) at the cost of background maintenance (VACUUM). Understanding this trade-off explains why autovacuum tuning is one of the most important DBA skills.

### 3. WAL Is More Than Crash Recovery
WAL serves four distinct purposes:
- **Crash recovery** (primary)
- **Point-in-time recovery** (PITR via WAL archiving)
- **Streaming replication** (sending WAL to standby servers)
- **Logical replication** (decoding WAL into logical change streams)

The design of a single WAL serving all these purposes is an elegant architectural decision, but it also means WAL volume directly impacts replication lag and backup storage.

### 4. The Planner Is Only as Good as Its Statistics
A common source of slow queries is the planner making poor decisions based on stale or inaccurate statistics. The `pg_statistic` catalog stores histograms, most-common-values, and distinct counts. Running `ANALYZE` regularly (or relying on autovacuum's auto-analyze) is critical. Cross-column correlations are a known blind spot — the planner assumes column values are independent, which can lead to severely underestimated join cardinalities.

### 5. B-Tree Concurrency Design Is Non-Trivial
The Lehman-Yao B-tree design allows concurrent reads and writes without holding locks across the entire root-to-leaf path. Right-links enable readers to follow page splits without restarting from the root. This is a nuanced solution to a hard problem — and it's why PostgreSQL's B-tree implementation (`nbtree`) is one of the most carefully maintained parts of the codebase.

---

## References

1. PostgreSQL Source Code: https://github.com/postgres/postgres
   - Buffer Manager: `src/backend/storage/buffer/bufmgr.c`
   - B-Tree: `src/backend/access/nbtree/nbtinsert.c`, `nbtree.c`, `nbtsearch.c`
   - MVCC/Heap: `src/backend/access/heap/heapam.c`, `heapam_visibility.c`
   - WAL: `src/backend/access/transam/xlog.c`
2. "The Internals of PostgreSQL" by Hironobu Suzuki: https://www.interdb.jp/pg/
3. PostgreSQL Documentation - "Database Physical Storage": https://www.postgresql.org/docs/current/storage.html
4. PostgreSQL Documentation - "WAL Configuration": https://www.postgresql.org/docs/current/wal-configuration.html
5. Lehman, P.L. and Yao, S.B., "Efficient Locking for Concurrent Operations on B-Trees" — ACM TODS, 1981
6. "Looking Inside PostgreSQL's Buffer Manager" — Bruce Momjian
7. PostgreSQL Wiki - "MVCC Unmasked": https://wiki.postgresql.org/wiki/MVCC
