# MySQL / InnoDB Storage Engine

## 1. Problem Background

### Why InnoDB?

MySQL supports multiple storage engines through its **pluggable storage engine architecture** — a unique design decision that separates the SQL layer (parsing, optimization, connection handling) from the storage layer (data persistence, indexing, transactions). InnoDB became MySQL's default storage engine in version 5.5 (2010), replacing MyISAM.

**The problem InnoDB solves**: Before InnoDB, MySQL's MyISAM engine offered fast reads but had critical limitations:
- No transaction support (no ACID compliance)
- Table-level locking only (a single write blocks all other operations)
- No crash recovery (corrupted tables after unexpected shutdowns)

InnoDB was originally developed by Innobase Oy (founded by Heikki Tuuri) to bring **transactional, crash-safe storage** to MySQL. It draws heavily from Oracle's architecture, particularly:
- **Clustered indexes** for data organization
- **MVCC** via undo logs (Oracle-style)
- **Redo/undo logging** for durability and rollback

### Historical Context

```
Timeline:
1995 ─── MySQL 1.0 (MyISAM only, no transactions)
1999 ─── InnoDB plugin released for MySQL
2001 ─── InnoDB integrated into MySQL
2005 ─── Oracle acquires Innobase Oy
2008 ─── Sun acquires MySQL AB
2010 ─── Oracle acquires Sun; InnoDB becomes default engine in MySQL 5.5
2016 ─── MySQL 8.0 development begins (InnoDB-centric improvements)
```

The key architectural question InnoDB answers differently from PostgreSQL is: **How should multi-version concurrency control be implemented?** InnoDB chose the Oracle approach (in-place updates + undo logs) while PostgreSQL chose the append-only approach (multiple physical tuple versions). This single decision cascades into fundamentally different system behaviors.

---

## 2. Architecture Overview

### MySQL Layered Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    Connection Layer                           │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────────────┐    │
│  │ Connection  │ │ Thread Pool│ │  Authentication      │    │
│  │ Handling    │ │ (or 1:1    │ │  & Authorization     │    │
│  │             │ │  threads)  │ │                      │    │
│  └─────────────┘ └─────────────┘ └─────────────────────┘    │
├──────────────────────────────────────────────────────────────┤
│                     SQL Layer                                │
│  ┌────────┐ ┌──────────┐ ┌──────────┐ ┌───────────────┐    │
│  │ Parser │ │Optimizer │ │ Executor │ │ Query Cache   │    │
│  │        │ │(cost-    │ │          │ │ (removed in   │    │
│  │        │ │ based)   │ │          │ │  MySQL 8.0)   │    │
│  └────────┘ └──────────┘ └──────────┘ └───────────────┘    │
├──────────────────────────────────────────────────────────────┤
│                  Storage Engine API                           │
│              (Handler interface: ha_*)                        │
│  ┌──────────────────────────────────────────────────────┐    │
│  │   handler::write_row()                               │    │
│  │   handler::update_row()                              │    │
│  │   handler::delete_row()                              │    │
│  │   handler::index_read()                              │    │
│  │   handler::rnd_next()                                │    │
│  └──────────────────────────────────────────────────────┘    │
├──────────────────────────────────────────────────────────────┤
│                     InnoDB Engine                             │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  ┌──────────┐ ┌──────────┐ ┌────────┐ ┌──────────┐  │    │
│  │  │  Buffer  │ │ Change   │ │Adaptive│ │  Log     │  │    │
│  │  │  Pool    │ │ Buffer   │ │ Hash   │ │  Buffer  │  │    │
│  │  │          │ │          │ │ Index  │ │          │  │    │
│  │  └──────────┘ └──────────┘ └────────┘ └──────────┘  │    │
│  │                                                      │    │
│  │  ┌──────────┐ ┌──────────┐ ┌─────────────────────┐  │    │
│  │  │  Undo    │ │  Redo    │ │  Lock Manager       │  │    │
│  │  │  Logs    │ │  Logs    │ │  (row + gap locks)  │  │    │
│  │  └──────────┘ └──────────┘ └─────────────────────┘  │    │
│  └──────────────────────────────────────────────────────┘    │
├──────────────────────────────────────────────────────────────┤
│                     Disk Storage                             │
│  ┌───────────────┐ ┌───────────┐ ┌────────────────────┐     │
│  │  Tablespace   │ │ Redo Log  │ │   Undo Tablespace  │     │
│  │  Files (.ibd) │ │ Files     │ │                    │     │
│  └───────────────┘ └───────────┘ └────────────────────┘     │
└──────────────────────────────────────────────────────────────┘
```

### Data Flow: Write Path

```
INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com');

1. SQL Layer parses, optimizes, passes to InnoDB handler

2. InnoDB Write Path:
   ┌─────────────┐
   │  Find page  │  Locate the leaf page of the clustered index
   │  in Buffer  │  where this row belongs (by primary key)
   │  Pool       │
   └──────┬──────┘
          │
   ┌──────▼──────┐
   │ Write Undo  │  Write undo record (for potential rollback)
   │ Log Record  │  Contains enough info to reverse the INSERT
   └──────┬──────┘
          │
   ┌──────▼──────┐
   │ Modify Page │  Insert the row into the page (in-memory)
   │ in Buffer   │  Page is now marked "dirty"
   │ Pool        │
   └──────┬──────┘
          │
   ┌──────▼──────┐
   │ Write Redo  │  Write redo log record to log buffer
   │ Log Record  │  (physical changes to the page)
   └──────┬──────┘
          │
   ┌──────▼──────┐
   │   COMMIT    │  Flush redo log buffer to disk (fsync)
   │             │  Transaction is now durable
   └──────┬──────┘
          │
          ▼
   Dirty pages flushed to tablespace files later
   (by page cleaner threads, during checkpointing)
```

---

## 3. Internal Design

### 3.1 Clustered Indexes

The **most architecturally significant** feature of InnoDB is the **clustered index**. Unlike PostgreSQL's heap storage, InnoDB stores table data **inside the leaf nodes of the primary key's B+Tree**.

```
Clustered Index (Primary Key: id):

              ┌─────────────────────────┐
              │      Root Page          │
              │   [id=500] [id=1000]    │
              └────┬──────────┬─────────┘
                   │          │
         ┌─────────▼──┐  ┌───▼──────────┐
         │ Internal   │  │  Internal    │
         │ [id=100]   │  │  [id=700]    │
         │ [id=200]   │  │  [id=800]    │
         │ [id=300]   │  │  [id=900]    │
         └─┬───┬───┬──┘  └──────────────┘
           │   │   │
    ┌──────▼┐ ┌▼────┐ ┌───▼──────────────────────┐
    │ Leaf  │ │Leaf │ │ Leaf Page               │
    │ Page  │ │Page │ │ ┌─────────────────────┐  │
    │       │ │     │ │ │Row: id=201          │  │
    │ Rows  │ │Rows │ │ │  name='Alice'       │  │  ← FULL ROW DATA
    │ 1-100 │ │101- │ │ │  email='a@mail.com' │  │     stored in leaf
    │       │ │200  │ │ │  created='2024-01-01'│  │
    └───────┘ └─────┘ │ ├─────────────────────┤  │
                      │ │Row: id=202          │  │
                      │ │  name='Bob'         │  │
                      │ │  ...                │  │
                      │ └─────────────────────┘  │
                      └──────────────────────────┘
```

**Why clustered indexes improve lookup performance:**

1. **Primary key lookups traverse one B+Tree** — directly landing on the row data. No separate heap fetch needed.
2. **Range queries on PK are sequential I/O** — rows with adjacent primary keys are stored in adjacent leaf pages.
3. **Fewer random I/Os** — data locality is guaranteed for PK-ordered access.

**The trade-off**: 
- Inserts with random PKs (e.g., UUIDs) cause **page splits** and random I/O (bad for write performance). Auto-increment integers avoid this.
- Secondary indexes carry extra overhead (see below).

### 3.2 Secondary Indexes

Secondary indexes in InnoDB do **NOT** point directly to the physical row location. Instead, they store the **primary key value** as the row pointer.

```
Secondary Index (on column: email):

    B+Tree for idx_email:
    ┌──────────────────────────────────────┐
    │  Leaf Page                           │
    │  ┌──────────────────────────────┐    │
    │  │ 'alice@mail.com' → PK=201   │    │  ← stores email + primary key
    │  │ 'bob@mail.com'   → PK=202   │    │
    │  │ 'carol@mail.com' → PK=150   │    │
    │  └──────────────────────────────┘    │
    └──────────────────────────────────────┘

    Secondary Index Lookup:
    
    SELECT * FROM users WHERE email = 'alice@mail.com';
    
    Step 1: Search secondary index → find PK=201
    Step 2: Search clustered index (primary key B+Tree) → find row data
    
    This is called a "double lookup" or "bookmark lookup"
```

**Why this design?**
- If InnoDB stored physical addresses (like PostgreSQL's TID), every page split in the clustered index would require updating **all secondary indexes**. By storing the PK value instead, page reorganization doesn't affect secondary indexes.
- Trade-off: secondary index lookups require **two B+Tree traversals** instead of one. But this is offset by the stability benefit — secondary indexes never need updating due to row movement.

**Covering Index Optimization**: If a secondary index contains all columns needed by a query, InnoDB can answer the query from the index alone (no clustered index lookup needed).

```sql
-- If index is: CREATE INDEX idx_email_name ON users(email, name);
-- This query uses a covering index:
SELECT name FROM users WHERE email = 'alice@mail.com';
-- No need to look up the clustered index!
```

### 3.3 Buffer Pool

The InnoDB Buffer Pool is the **in-memory cache** for data pages, index pages, undo pages, and change buffer pages. It is InnoDB's most performance-critical component.

```
Buffer Pool Architecture:
┌─────────────────────────────────────────────────────┐
│                   Buffer Pool                        │
│                (innodb_buffer_pool_size)              │
│                                                      │
│  ┌─────────────────────────────────────────────┐     │
│  │              LRU List                        │     │
│  │                                              │     │
│  │  ┌────────────────────┬────────────────────┐ │     │
│  │  │   Young Sublist    │   Old Sublist      │ │     │
│  │  │   (hot pages)      │   (5/8 point)      │ │     │
│  │  │   ──────────────── │ ──────────────     │ │     │
│  │  │   Recently used    │  Newly loaded      │ │     │
│  │  │   pages stay here  │  pages start here  │ │     │
│  │  └────────────────────┴────────────────────┘ │     │
│  └─────────────────────────────────────────────┘     │
│                                                      │
│  ┌───────────────────┐  ┌────────────────────────┐   │
│  │   Free List       │  │   Flush List           │   │
│  │   (unused pages)  │  │   (dirty pages ordered │   │
│  │                   │  │    by oldest_lsn)      │   │
│  └───────────────────┘  └────────────────────────┘   │
│                                                      │
│  ┌───────────────────────────────────────────────┐   │
│  │   Adaptive Hash Index (AHI)                    │   │
│  │   (automatic hash index on hot B+Tree pages)   │   │
│  │   Built on observed access patterns            │   │
│  └───────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

**Midpoint Insertion Strategy (Why not pure LRU?)**:

New pages are inserted at the **midpoint** (between young and old sublists), not at the head. This prevents **buffer pool pollution** from full table scans — a sequential scan reads every page once but shouldn't evict frequently-used pages from the cache.

```
Full table scan without midpoint:
[Hot pg] [Hot pg] [Hot pg] [Scan pg1] [Scan pg2] ... → Hot pages evicted!

Full table scan WITH midpoint:
Young: [Hot pg] [Hot pg] [Hot pg] | Old: [Scan pg1] [Scan pg2] ...
                                         ↑ scan pages start here and get evicted quickly
                                           (unless accessed again within innodb_old_blocks_time)
```

### 3.4 Undo Logs

Undo logs are InnoDB's mechanism for **transaction rollback** and **MVCC read consistency**. They store the information needed to reverse a change.

```
UPDATE users SET name = 'Bob' WHERE id = 1;

Before:  Row in clustered index: {id=1, name='Alice', email='a@mail.com'}

Step 1: Write undo log record:
    ┌───────────────────────────────────┐
    │ Undo Record:                     │
    │   Type: TRX_UNDO_UPD_EXIST_REC   │
    │   Table ID: users                │
    │   Primary Key: id=1              │
    │   Old values: name='Alice'       │
    │   Trx ID: 500                    │
    │   Roll pointer → previous undo    │
    └───────────────────────────────────┘

Step 2: Update row in-place:
    Row: {id=1, name='Bob', email='a@mail.com'}
    Hidden columns:
      DB_TRX_ID = 500        ← transaction that last modified this row
      DB_ROLL_PTR → undo rec ← pointer to undo log record
```

**MVCC via Undo Chains:**

When a reader needs to see an older version of a row, InnoDB follows the **undo chain** backwards:

```
Current row: {name='Charlie', trx_id=600, roll_ptr → undo_3}
                                                      │
    Undo record 3: {name='Bob', trx_id=500}    ◄─────┘
                    roll_ptr → undo_2
                               │
    Undo record 2: {name='Alice', trx_id=400}  ◄─────┘
                    roll_ptr → NULL (original insert)


Reader with snapshot at trx_id=450:
  1. Read current row: trx_id=600 > 450 → too new, follow roll_ptr
  2. Undo record 3: trx_id=500 > 450 → still too new, follow roll_ptr
  3. Undo record 2: trx_id=400 ≤ 450 → VISIBLE! Return {name='Alice'}
```

**Critical comparison with PostgreSQL:**

| Aspect | InnoDB (Undo-based MVCC) | PostgreSQL (Heap-based MVCC) |
|---|---|---|
| Where old versions live | Undo log (separate storage) | Heap pages (alongside current data) |
| Update mechanism | In-place modification | New tuple appended to heap |
| Cleanup | Purge thread removes old undo records | VACUUM removes dead tuples |
| Read overhead | Long undo chains → slow reads | Dead tuples → bloated scans |
| Write overhead | Undo record + in-place update | New tuple + old tuple header update |
| Storage impact | Data pages stay compact | Data pages bloat over time |

### 3.5 Redo Logs

Redo logs provide **durability** — the guarantee that committed transactions survive crashes. They are InnoDB's equivalent of PostgreSQL's WAL.

```
Redo Log Architecture:

    ┌──────────────┐
    │  Log Buffer  │    (in memory: innodb_log_buffer_size)
    │  [record 1]  │
    │  [record 2]  │
    │  [record 3]  │
    └──────┬───────┘
           │ flush (on commit or when buffer full)
           ▼
    ┌──────────────┐  ┌──────────────┐
    │  ib_redo_    │  │  ib_redo_    │     Circular redo log files
    │  log_0       │──│  log_1       │──── (fixed size, overwritten)
    │              │  │              │
    └──────────────┘  └──────────────┘
```

**Why InnoDB needs BOTH redo and undo logs:**

```
                    REDO LOG                      UNDO LOG
                    ────────                      ────────
Purpose:            Replay committed changes      Reverse uncommitted changes
                    after crash                   (rollback)

Used during:        Crash RECOVERY                Transaction ROLLBACK +
                    (redo phase)                  MVCC read consistency +
                                                  Crash recovery (undo phase)

Contains:           Physical page changes         Logical old values
                    ("set byte X on page Y")      ("column was 'Alice'")

Lifecycle:          Recycled after checkpoint      Purged after no active
                                                   reader needs old version

Write pattern:      Append-only, circular          Append-only, segment-based
```

**Crash Recovery Process:**

```
Crash → Restart → InnoDB Recovery:

Phase 1: REDO (roll forward)
    ┌──────────────────────────────────────────────┐
    │ Scan redo log from last checkpoint            │
    │ Apply all redo records to data pages          │
    │ (whether transactions committed or not)       │
    │                                               │
    │ Result: All pages reflect state at crash time │
    └──────────────────────────────────────────────┘
                        │
                        ▼
Phase 2: UNDO (roll back)
    ┌──────────────────────────────────────────────┐
    │ Identify uncommitted transactions            │
    │ (transactions in TRX_ACTIVE state)           │
    │                                               │
    │ For each uncommitted transaction:            │
    │   Follow undo chain                          │
    │   Reverse all changes                        │
    │                                               │
    │ Result: Only committed changes remain        │
    └──────────────────────────────────────────────┘
                        │
                        ▼
    Database is consistent → accept connections
```

### 3.6 Locking Mechanisms

InnoDB implements **row-level locking** with several lock types designed to handle different concurrency scenarios:

#### Lock Types

```
┌──────────────────────────────────────────────────────────┐
│                    InnoDB Lock Types                       │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  Record Locks:   Lock on a specific index record         │
│                  (locks the index entry, not the row)     │
│                                                          │
│  Gap Locks:      Lock on the gap BETWEEN index records   │
│                  Prevents phantom reads                   │
│                  Example: gap before id=10, between       │
│                  id=10 and id=20, after id=20             │
│                                                          │
│  Next-Key Locks: Record lock + gap lock on the gap       │
│                  before the record (default in RR)       │
│                  Prevents both duplicate inserts and      │
│                  phantom reads                            │
│                                                          │
│  Insert Intention Locks: Special gap lock for INSERT     │
│                  Multiple inserts into the same gap       │
│                  don't block each other if they have      │
│                  different key values                     │
│                                                          │
│  Auto-Inc Locks: Table-level lock for AUTO_INCREMENT     │
│                  (lightweight mutex in MySQL 8.0+)       │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

#### Gap Locks: Why They Exist

```
Without gap locks (Phantom Read problem):

    Transaction 1:                     Transaction 2:
    BEGIN;
    SELECT * FROM users 
    WHERE age BETWEEN 20 AND 30;
    → Returns: {Alice(25), Bob(28)}
                                       BEGIN;
                                       INSERT INTO users VALUES 
                                         ('Carol', 27);
                                       COMMIT;
    SELECT * FROM users 
    WHERE age BETWEEN 20 AND 30;
    → Returns: {Alice(25), Carol(27), Bob(28)}  ← PHANTOM!
    COMMIT;

With gap locks (REPEATABLE READ):

    Transaction 1:                     Transaction 2:
    BEGIN;
    SELECT * FROM users 
    WHERE age BETWEEN 20 AND 30
    FOR UPDATE;
    → Returns: {Alice(25), Bob(28)}
    → Gap locks placed on:
       (20, 25), (25, 28), (28, 30)
                                       BEGIN;
                                       INSERT INTO users VALUES 
                                         ('Carol', 27);
                                       → BLOCKED by gap lock on (25, 28)!
                                       → Waits...
    COMMIT;
    → Gap locks released
                                       → INSERT proceeds
                                       COMMIT;
```

### 3.7 Isolation Levels in InnoDB

| Level | Dirty Read | Non-Repeatable Read | Phantom Read | Implementation |
|---|---|---|---|---|
| READ UNCOMMITTED | Yes | Yes | Yes | No MVCC, reads current data |
| READ COMMITTED | No | Yes | Yes | Fresh snapshot per statement |
| REPEATABLE READ (default) | No | No | No* | Snapshot at first read + gap locks |
| SERIALIZABLE | No | No | No | Like RR but all reads are `SELECT ... LOCK IN SHARE MODE` |

*InnoDB's REPEATABLE READ prevents phantoms through gap locking — this goes beyond the SQL standard's requirements.

---

## 4. Design Trade-Offs

### InnoDB vs. PostgreSQL: Fundamental MVCC Trade-Offs

```
                  InnoDB                         PostgreSQL
                  ──────                         ──────────

UPDATE:           In-place + undo log            Append new tuple to heap
                  ┌────────┐                     ┌────────┐ ┌────────┐
                  │ Row v2 │ (in place)          │ Row v1 │ │ Row v2 │
                  │        │ → undo has v1       │ (dead) │ │ (live) │
                  └────────┘                     └────────┘ └────────┘

Data Page         Compact (only live data)       Bloated (dead tuples remain)
Size:             

Read Old          Follow undo chain              Directly visible on heap page
Version:          (random I/O to undo space)     (sequential scan sees all versions)

Cleanup:          Purge thread cleans undo        VACUUM removes dead tuples
                  (lightweight, background)       (can be heavyweight, blocks reuse)

Write             Higher (undo record + in-place  Lower (just append new tuple +
Overhead:         update + redo record)           update old header + WAL record)

Long-Running      Undo chain grows → reads slow   Dead tuples accumulate → bloat
Transaction       (but data pages stay clean)     (VACUUM can't clean referenced tuples)
Impact:
```

### Clustered vs. Heap Storage

| Scenario | Clustered (InnoDB) | Heap (PostgreSQL) |
|---|---|---|
| Primary key point lookup | **Faster** — single B+Tree traversal | Slower — index + heap fetch |
| PK range scan | **Faster** — sequential leaf page reads | Slower — random heap fetches |
| Secondary index lookup | Slower — double B+Tree traversal | **Faster** — index + single heap fetch |
| INSERT with sequential PK | **Fast** — append to last leaf page | Fast — append to any heap page |
| INSERT with random PK | **Slow** — random page splits | Fast — append to any heap page |
| UPDATE non-indexed column | Efficient — in-place update | Creates new tuple version |
| Full table scan | Reads pages in PK order | Reads pages in physical order |

### Why PostgreSQL Chose a Different MVCC Model

PostgreSQL's designers made a deliberate choice against undo-based MVCC:

1. **Simplicity**: No undo log management, no purge threads, no undo segment sizing
2. **Consistent read performance**: No long undo chain traversals for old versions
3. **HOT (Heap-Only Tuples)**: When non-indexed columns are updated and the new tuple fits on the same page, PostgreSQL can avoid updating indexes entirely — something InnoDB can't do because the clustered index IS the data
4. **Simpler crash recovery**: No undo phase needed, just WAL replay

The cost is VACUUM, which PostgreSQL's community has been continuously improving (autovacuum, visibility maps, parallel vacuum).

---

## 5. Experiments / Observations

### Experiment 1: Clustered Index Ordering Impact

```sql
-- Test: Impact of PK ordering on insert performance

-- Schema 1: Auto-increment PK (sequential, cache-friendly)
CREATE TABLE orders_auto (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    data VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- Schema 2: UUID PK (random, causes page splits)
CREATE TABLE orders_uuid (
    id CHAR(36) PRIMARY KEY DEFAULT (UUID()),
    data VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- Insert 1 million rows into each and compare:
-- Expected results:
-- orders_auto:  ~30-40 seconds (sequential writes, no page splits)
-- orders_uuid:  ~90-120 seconds (random I/O, frequent page splits)
-- orders_uuid file size: ~40-50% larger due to half-filled split pages
```

**Observation**: UUID primary keys in InnoDB are a well-known anti-pattern. The random distribution causes:
- Frequent B+Tree page splits
- Low page fill factors (~50-70% vs ~90%+ for sequential)
- Higher buffer pool churn (random pages can't be cached effectively)

### Experiment 2: Undo Log Impact on Long Transactions

```sql
-- Session 1: Start a long-running transaction
BEGIN;
SELECT * FROM large_table LIMIT 1;  -- Establishes read view
-- Leave transaction OPEN for 10 minutes

-- Session 2: Run continuous updates
-- (This creates undo log entries that can't be purged
--  because Session 1's read view still needs them)

-- Monitor undo log growth:
SELECT 
    count AS undo_entries,
    trx_id
FROM information_schema.innodb_metrics 
WHERE name = 'trx_rseg_history_len';

-- Expected: History length grows continuously
-- because purge can't clean undo records older than Session 1's view
```

### Experiment 3: Lock Contention Analysis

```sql
-- Demonstrate gap locking behavior:

-- Session 1:
SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
BEGIN;
SELECT * FROM users WHERE age BETWEEN 20 AND 30 FOR UPDATE;

-- Session 2:
BEGIN;
INSERT INTO users (name, age) VALUES ('Test', 25);
-- Expected: BLOCKED (gap lock prevents insert in range 20-30)

-- Check locks:
SELECT * FROM performance_schema.data_locks;
-- Shows: RECORD locks and GAP locks held by Session 1

-- Session 3:
BEGIN;
INSERT INTO users (name, age) VALUES ('Test2', 35);
-- Expected: SUCCEEDS (outside the gap-locked range)
```

### Experiment 4: Buffer Pool Analysis

```sql
-- Check buffer pool utilization:
SELECT 
    pool_id,
    pool_size,
    free_buffers,
    database_pages,
    old_database_pages,
    modified_db_pages,
    pages_made_young,
    pages_made_not_young,
    hit_rate
FROM information_schema.innodb_buffer_pool_stats;

-- Check which tables consume the most buffer pool:
SELECT 
    table_name,
    COUNT(*) AS pages_in_buffer,
    SUM(is_stale) AS stale_pages
FROM information_schema.innodb_buffer_page
WHERE table_name IS NOT NULL
GROUP BY table_name
ORDER BY pages_in_buffer DESC
LIMIT 10;
```

---

## 6. Key Learnings

### 1. Clustered Indexes Are a Double-Edged Sword
InnoDB's decision to store data in the primary key B+Tree provides excellent performance for primary key lookups and range scans — the most common access pattern. But it imposes a **hidden tax on secondary indexes** (double lookup) and makes **PK choice critical** for write performance. Auto-increment integers are almost always the right choice for InnoDB PKs, not UUIDs.

### 2. Redo + Undo Is More Complex but More Space-Efficient
The dual-log architecture (redo for durability, undo for rollback/MVCC) is more complex than PostgreSQL's single-WAL approach. But it keeps data pages compact — only live data on data pages, with old versions in a separate undo space that's efficiently managed. This is why InnoDB tables don't need VACUUM-style maintenance for space reclamation.

### 3. Gap Locking Prevents Phantoms but Can Surprise You
InnoDB's gap locks are a clever solution to phantom reads at the REPEATABLE READ level (stronger than the SQL standard requires). But they can cause unexpected deadlocks and reduced concurrency in workloads with many range queries and inserts in the same key space. Understanding gap locks is essential for debugging InnoDB lock contention.

### 4. The Buffer Pool Is Everything
InnoDB's performance is dominated by buffer pool hit rates. The recommendation of setting `innodb_buffer_pool_size` to 70-80% of available RAM (on a dedicated database server) reflects how central this component is. The midpoint insertion strategy and adaptive hash index are both optimizations to maximize buffer pool effectiveness.

### 5. MVCC Model Choice Has System-Wide Implications
InnoDB's Oracle-style MVCC (in-place updates + undo) vs. PostgreSQL's append-only MVCC is not just a storage decision — it cascades into:
- How cleanup works (purge vs. VACUUM)
- How indexes are structured (clustered vs. heap)
- How long transactions impact the system (undo growth vs. bloat)
- How crash recovery works (redo+undo phases vs. WAL-only replay)

Neither approach is universally better. InnoDB's model favors **OLTP workloads with frequent updates** (data pages stay compact). PostgreSQL's model favors **read-heavy and append-heavy workloads** (no undo chain traversal for old versions).

---

## References

1. MySQL Documentation - InnoDB Architecture: https://dev.mysql.com/doc/refman/8.0/en/innodb-architecture.html
2. "MySQL Internals Manual": https://dev.mysql.com/doc/internals/en/
3. "High Performance MySQL" by Baron Schwartz, Peter Zaitsev, Vadim Tkachenko
4. InnoDB Source Code: `storage/innobase/` in MySQL source
5. Jeremy Cole, "InnoDB: A Journey to the Core" — Percona Live Talks
6. "Understanding InnoDB Clustered Indexes" — Percona Blog
7. MySQL Documentation - InnoDB Locking: https://dev.mysql.com/doc/refman/8.0/en/innodb-locking.html
8. Percona, "InnoDB Buffer Pool Internals": https://www.percona.com/blog/
9. Oracle, "InnoDB Undo Logs": https://dev.mysql.com/doc/refman/8.0/en/innodb-undo-logs.html
