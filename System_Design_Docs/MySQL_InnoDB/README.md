# MySQL InnoDB Architecture

## 1. Overview

InnoDB is MySQL's default storage engine since MySQL 5.5, replacing MyISAM. It provides full ACID compliance, row-level locking, and MVCC — making it suitable for transactional workloads. Unlike PostgreSQL's process-per-connection model, MySQL uses a thread-per-connection model within a single server process. InnoDB's architecture is built around a buffer pool, redo/undo logs, and a clustered index structure that fundamentally shapes how data is stored and accessed.

---

## 2. InnoDB Architecture Overview

### 2.1 In-Memory Structures

```
┌─────────────────────────────────────────────────────────────┐
│                    InnoDB Memory Structures                  │
├─────────────────────────────────────────────────────────────┤
│ Buffer Pool (largest memory consumer)                        │
│  - Caches data pages and index pages                        │
│  - Uses LRU replacement with midpoint insertion             │
│  - Configurable size (typically 50-75% of RAM)              │
├─────────────────────────────────────────────────────────────┤
│ Change Buffer                                               │
│  - Caches secondary index changes when pages not in buffer  │
│  - Merged later when pages are read                         │
│  - Reduces random I/O for non-unique secondary indexes      │
├─────────────────────────────────────────────────────────────┤
│ Adaptive Hash Index (AHI)                                   │
│  - In-memory hash table built on top of B-tree indexes      │
│  - Automatically created for frequently accessed pages      │
│  - Can be disabled for write-heavy workloads                │
├─────────────────────────────────────────────────────────────┤
│ Log Buffer                                                  │
│  - In-memory buffer for redo log entries                    │
│  - Flushed to disk on transaction commit (or periodically) │
│  - Size: typically 16-64MB                                  │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 On-Disk Structures

```
┌─────────────────────────────────────────────────────────────┐
│                    InnoDB On-Disk Structures                 │
├─────────────────────────────────────────────────────────────┤
│ Tablespaces (.ibd files or ibdata1 system tablespace)       │
│  - Data pages (16KB default)                                │
│  - Index pages                                              │
│  - Undo tablespace                                          │
│  - Doublewrite buffer pages                                 │
├─────────────────────────────────────────────────────────────┤
│ Redo Log (ib_logfile0, ib_logfile1)                         │
│  - Circular log files for crash recovery                    │
│  - Physiological logging (page + offset + new value)        │
├─────────────────────────────────────────────────────────────┤
│ Undo Log                                                    │
│  - Stores old versions for MVCC and rollback              │
│  - Stored in undo tablespace or rollback segments           │
├─────────────────────────────────────────────────────────────┤
│ Doublewrite Buffer                                          │
│  - Write-once area for page writes                          │
│  - Protects against torn pages during crashes                 │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. Why InnoDB Needs Both Undo and Redo Logs

This is one of the most important architectural decisions in InnoDB. Both logs serve fundamentally different purposes:

### 3.1 Redo Log: Forward Recovery

**Purpose:** Ensure durability — committed transactions survive crashes.

**What it logs:** Physical changes to pages ("page X, offset Y, changed from A to B").

**When it's written:**
1. Transaction modifies a page in the buffer pool
2. Change is recorded in the log buffer
3. On COMMIT, log buffer is flushed to redo log files (fsync)
4. Data pages may still be dirty in memory

**Why it's needed:**
If the server crashes after COMMIT but before dirty pages are written to disk, the redo log contains enough information to **reapply** the changes. Without redo, committed data would be lost.

**Redo Log Structure:**
```
Redo Log Entry (Mini-Transaction / MTR):
┌────────────────────────────────────────┐
│ Type: MLOG_2BYTES, MLOG_4BYTES, etc. │
│ Space ID + Page Number                 │
│ Offset within page                     │
│ Old value (optional)                   │
│ New value                              │
└────────────────────────────────────────┘
```

**Key Characteristics:**
- **Physiological logging**: Logs physical page locations but logical operations
- **Idempotent**: Can be replayed multiple times safely
- **Circular**: Fixed-size files written in a ring buffer fashion
- **Write-ahead**: Redo is written before dirty pages are flushed

### 3.2 Undo Log: Backward Recovery

**Purpose:** Enable rollback and MVCC — provide old versions of rows.

**What it logs:** Logical inverse operations ("DELETE row X" → undo is "INSERT row X with old values").

**When it's written:**
1. Transaction modifies a row
2. Before changing the row, the old version is written to undo log
3. The row's `DB_ROLL_PTR` points to the undo record
4. On ROLLBACK, undo records are applied in reverse order

**Why it's needed:**
- **Rollback**: If a transaction aborts, undo records restore the previous state
- **MVCC**: Read transactions use undo records to construct consistent snapshots of old data
- **Isolation**: Prevents readers from seeing uncommitted changes

**Undo Log Structure:**
```
Undo Record:
┌────────────────────────────────────────┐
│ DB_TRX_ID: Transaction that created it  │
│ DB_ROLL_PTR: Pointer to previous undo  │
│ Type: INSERT_UNDO or UPDATE_UNDO        │
│ Table ID                                │
│ Primary Key Columns (for locating row)  │
│ Old Column Values (for UPDATE_UNDO)     │
└────────────────────────────────────────┘
```

### 3.3 The Symbiotic Relationship

```
Transaction Flow:

BEGIN;
UPDATE users SET balance = 90 WHERE id = 1;  -- balance was 100

Step 1: Write Undo Record
  "Old value: balance = 100"
  → Stored in undo log segment

Step 2: Modify Buffer Pool Page
  "Page 5, offset 200: change 100 → 90"
  → Page marked dirty

Step 3: Write Redo Log Record
  "Page 5, offset 200: new value = 90"
  → Stored in log buffer

COMMIT;
Step 4: Flush Redo Log to Disk
  → fsync() redo log files
  → COMMIT returns to client

Later (async):
Step 5: Dirty Page Written to Disk
  → Via checkpoint or LRU eviction

CRASH SCENARIO (after COMMIT, before page flush):
  → On restart: Redo log replayed → balance = 90 ✓

ROLLBACK SCENARIO (instead of COMMIT):
  → Undo records applied → balance restored to 100 ✓
```

**Why Both Are Essential:**

| Aspect | Redo Log | Undo Log |
|--------|----------|----------|
| **Direction** | Forward (redo changes) | Backward (undo changes) |
| **Purpose** | Durability (D in ACID) | Atomicity + Isolation (A, I in ACID) |
| **When Used** | Crash recovery | Rollback + MVCC reads |
| **Log Type** | Physical (page-level) | Logical (row-level) |
| **Persistence** | Must survive crash | Can be purged after no longer needed |
| **Lifetime** | Until checkpoint advances past it | Until no transaction needs the old version |

**Key Insight:** Redo log answers "What must be reapplied after a crash?" Undo log answers "What must be reversed if we abort, or what old version should readers see?"

---

## 4. Clustered Indexes

### 4.1 What is a Clustered Index?

InnoDB stores table data in a **clustered index** (also called a "primary index"):

- The PRIMARY KEY determines the physical order of rows in the table
- The leaf pages of the clustered index contain the **entire row data**
- The table IS the clustered index — there is no separate "heap" for row storage

```
Clustered Index Structure:
┌─────────────────────────────────────────────────────────┐
│ Internal Node (non-leaf)                                │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐                  │
│  │ Key: 10 │  │ Key: 20 │  │ Key: 30 │                  │
│  │ Ptr: P1 │  │ Ptr: P2 │  │ Ptr: P3 │                  │
│  └─────────┘  └─────────┘  └─────────┘                  │
└─────────────────────────────────────────────────────────┘
         │            │            │
         ▼            ▼            ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ Leaf Page P1 │ │ Leaf Page P2 │ │ Leaf Page P3 │
│ Key: 1-10    │ │ Key: 11-20   │ │ Key: 21-30   │
│ Contains:    │ │ Contains:    │ │ Contains:    │
│  Full rows   │ │  Full rows   │ │  Full rows   │
│  (all columns)│ │  (all columns)│ │  (all columns)│
└──────────────┘ └──────────────┘ └──────────────┘
```

### 4.2 Advantages of Clustered Indexes

1. **Fast Primary Key Lookups**: 
   - `SELECT * FROM users WHERE id = 5` requires exactly one B-tree traversal
   - No additional heap lookup needed (unlike PostgreSQL's heap + index model)

2. **Range Scan Efficiency**:
   - `SELECT * FROM orders WHERE order_date BETWEEN '2024-01-01' AND '2024-01-31'`
   - Rows are physically ordered by the clustered key
   - Sequential I/O instead of random I/O

3. **Data Locality**:
   - Related rows (same customer, same date range) are stored together
   - Better cache utilization
   - Fewer disk reads for range queries

4. **No Row Movement on Update** (if PK doesn't change):
   - Non-key updates happen in-place
   - No need to update indexes pointing to the row (except changed columns' indexes)

### 4.3 Disadvantages of Clustered Indexes

1. **Secondary Index Overhead**:
   - Secondary indexes store the PRIMARY KEY as their leaf value, not a physical row pointer
   - Lookup via secondary index: `Secondary Index → Primary Key → Clustered Index → Row Data`
   - This "double lookup" is more expensive than PostgreSQL's direct ctid lookup

2. **Insert Order Dependency**:
   - Random primary keys (UUIDs) cause page splits and fragmentation
   - Sequential keys (AUTO_INCREMENT) provide optimal insert performance

3. **Table Locking During Rebuild**:
   - Rebuilding the clustered index (e.g., `OPTIMIZE TABLE`) requires rewriting the entire table
   - Expensive for large tables

### 4.4 Secondary Indexes in InnoDB

```
Secondary Index Structure:
┌─────────────────────────────────────────────────────────┐
│ Leaf Page                                               │
│  ┌─────────────────┐  ┌─────────────────┐              │
│  │ Indexed Col: 'A'│  │ Indexed Col: 'B'│              │
│  │ PK Value: 42    │  │ PK Value: 17    │              │
│  └─────────────────┘  └─────────────────┘              │
└─────────────────────────────────────────────────────────┘
                              │
                              ▼
                    ┌─────────────────┐
                    │ Clustered Index │
                    │ PK = 42 or 17   │
                    │ → Full row data │
                    └─────────────────┘
```

**Key Difference from PostgreSQL:**
- PostgreSQL secondary indexes store `ctid` (physical row location)
- InnoDB secondary indexes store the PRIMARY KEY value
- InnoDB's approach is slower for lookups but immune to row movement (no index maintenance on non-key updates)

---

## 5. InnoDB's MVCC Model

### 5.1 How InnoDB MVCC Works

InnoDB implements MVCC using **undo logs** rather than tuple versioning like PostgreSQL:

**Hidden Columns in Every Row:**
```
┌─────────────────────────────────────────────────────────┐
│ User Columns (visible)                                  │
├─────────────────────────────────────────────────────────┤
│ DB_TRX_ID (6 bytes)                                     │
│  - Transaction ID that last modified this row           │
│  - Used for visibility checks                             │
├─────────────────────────────────────────────────────────┤
│ DB_ROLL_PTR (7 bytes)                                   │
│  - Pointer to undo log record                           │
│  - Used to reconstruct previous versions                │
├─────────────────────────────────────────────────────────┤
│ DB_ROW_ID (6 bytes, optional)                           │
│  - Auto-generated row ID if no PRIMARY KEY              │
└─────────────────────────────────────────────────────────┘
```

### 5.2 Read Views (Snapshots)

When a transaction starts (in REPEATABLE READ), InnoDB creates a **Read View**:

```
Read View Structure:
  m_low_limit_id:     Next transaction ID to be assigned
  m_up_limit_id:      Lowest active transaction ID
  m_creator_trx_id:   Transaction ID of creator
  m_ids:              List of all active transaction IDs
```

**Visibility Rules:**
1. If `DB_TRX_ID` == `m_creator_trx_id` → Visible (created by current transaction)
2. If `DB_TRX_ID` < `m_up_limit_id` → Visible (committed before snapshot)
3. If `DB_TRX_ID` >= `m_low_limit_id` → Invisible (future transaction)
4. If `DB_TRX_ID` is in `m_ids` → Invisible (still active)
5. Otherwise → Visible (committed between up_limit and low_limit)

### 5.3 Version Chain Traversal

When a row's `DB_TRX_ID` is invisible, InnoDB follows the undo chain:

```
Current Row (in buffer pool):
  DB_TRX_ID = 150, DB_ROLL_PTR → Undo Record 150
  Data: ('Alice', 26)

Undo Record 150:
  DB_TRX_ID = 150, DB_ROLL_PTR → Undo Record 120
  Old Data: ('Alice', 25)  ← This is what a read view sees

Undo Record 120:
  DB_TRX_ID = 120, DB_ROLL_PTR → NULL
  Old Data: ('Alice', 20)  ← Even older version
```

**Read Process:**
```
1. Read current row from clustered index
2. Check DB_TRX_ID against Read View
3. If visible → return row
4. If invisible → follow DB_ROLL_PTR to undo record
5. Reconstruct previous version
6. Check that version's DB_TRX_ID
7. Repeat until visible version found or chain exhausted
```

### 5.4 Secondary Index MVCC

Secondary indexes complicate MVCC because they don't store `DB_TRX_ID` or `DB_ROLL_PTR`:

**Approach:**
- Each secondary index leaf page stores `PAGE_MAX_TRX_ID` (highest trx_id that modified the page)
- If `PAGE_MAX_TRX_ID` < `m_up_limit_id` → all rows on page are visible (fast path)
- Otherwise, for each matching row:
  1. Look up the row in the clustered index using the PK
  2. Perform full MVCC visibility check on the clustered index row
  3. Verify the secondary index value still matches (it may have been updated)

**Performance Impact:**
- Secondary index scans with old Read Views are expensive
- Each matching row requires a clustered index lookup
- This is why covering indexes (where all query columns are in the index) are so valuable

### 5.5 Why PostgreSQL Chose a Different MVCC Model

**InnoDB Approach (Undo Log + Read View):**
- **Pros**: 
  - In-place updates (no table bloat)
  - No background vacuum process needed
  - Clustered index provides fast primary key access
- **Cons**:
  - Undo log management is complex
  - Long-running transactions block undo log purging
  - Secondary index MVCC is expensive
  - History is in undo log (separate structure), not in the table itself

**PostgreSQL Approach (Heap Tuple Versioning):**
- **Pros**:
  - Simple visibility check (xmin/xmax on the tuple itself)
  - No undo log to manage
  - Index entries point directly to heap (no double lookup)
  - Historical versions are "free" (just don't vacuum them)
- **Cons**:
  - Table bloat from dead tuples
  - Requires VACUUM process
  - Indexes may contain pointers to dead tuples
  - Updates are effectively DELETE + INSERT (expensive for wide rows)

**Key Architectural Difference:**
- **InnoDB**: The "truth" is the current version in the clustered index; old versions are reconstructed on-demand from undo logs
- **PostgreSQL**: All versions are "true" and coexist in the heap; visibility determines which one you see

**Why PostgreSQL Chose Heap Versioning:**
1. **Simplicity**: No separate undo log structure to manage
2. **Index Efficiency**: Indexes point directly to heap tuples (no PK lookup)
3. **Time Travel**: Historical data is physically present (enables temporal queries)
4. **No Purge Blocking**: Long transactions don't block cleanup (VACUUM is cooperative)
5. **Design Heritage**: PostgreSQL's design predates InnoDB's undo log approach; the heap model was simpler to implement correctly

---

## 6. Doublewrite Buffer

### 6.1 The Torn Page Problem

InnoDB pages are 16KB, but OS/filesystem writes are typically 4KB. If a crash occurs during a 16KB page write, the page may be partially written ("torn page").

**Without Protection:**
```
Original Page: [AAAA AAAA AAAA AAAA] (16KB)
During Write:  [AAAA BBBB ???? ????]  ← Crash here!
After Restart: Corrupted page (half old, half new)
```

Even with redo logs, replaying changes onto a corrupted page is impossible.

### 6.2 Doublewrite Buffer Solution

InnoDB writes each page **twice**:

```
Step 1: Write full 16KB page to Doublewrite Buffer (sequential I/O)
        Location: System tablespace (ibdata1) or dedicated file

Step 2: Write the same 16KB page to its actual location (random I/O)
        Location: Tablespace file (.ibd)

On Crash Recovery:
  If page in .ibd is corrupted (checksum mismatch):
    → Copy valid page from Doublewrite Buffer
    → Then apply redo logs
```

**Trade-offs:**
- **Cost**: Extra sequential write (~1-5% performance impact)
- **Benefit**: Guaranteed protection against torn pages
- **Size**: 2MB (128 pages) in memory + 2MB on disk

**Comparison with PostgreSQL:**
- PostgreSQL uses **Full Page Writes (FPW)** in WAL instead
- After each checkpoint, the first modification to a page writes the entire page image to WAL
- Similar protection, different mechanism (WAL vs dedicated buffer)

---

## 7. Change Buffer

### 7.1 Purpose

When a secondary index page is not in the buffer pool, modifying it requires a disk read. The **Change Buffer** caches these changes in memory:

```
INSERT/DELETE/UPDATE on secondary index column
         │
         ▼
Is index page in buffer pool?
    ├─ Yes → Modify page directly
    └─ No  → Record change in Change Buffer
                    │
                    ▼
            When page is later read:
                    │
                    ▼
            Merge buffered changes into page
```

**Benefits:**
- Reduces random I/O for secondary index maintenance
- Particularly effective for non-unique secondary indexes
- Merged during reads or background purge

**Trade-offs:**
- Increases memory usage
- Merge operations can cause read latency spikes
- Can be disabled for SSDs (`innodb_change_buffering = none`)

---

## 8. Checkpointing in InnoDB

### 8.1 Types of Checkpoints

**Sharp Checkpoint:**
- Flush ALL dirty pages to disk
- Blocks all writes during checkpoint
- Fast recovery but causes I/O spikes

**Fuzzy Checkpoint:**
- Flush dirty pages gradually in background
- LSN advances continuously
- No write blocking, but slower recovery

**InnoDB uses Fuzzy Checkpoints:**
- **Log Checkpoint**: Tracks the oldest modified page's LSN
- **Flush List**: Dirty pages ordered by modification LSN
- **Background Threads**: `page_cleaner` threads flush pages continuously

### 8.2 Checkpoint Process

```
1. Determine checkpoint LSN (oldest dirty page)
2. Write checkpoint record to redo log
3. Flush dirty pages from buffer pool (gradually)
4. Update system tablespace header with checkpoint info

Recovery starts from checkpoint LSN:
  - Redo log records before checkpoint are guaranteed to be on disk
  - Only need to replay from checkpoint LSN forward
```

---

## 9. Key Architectural Insights

1. **Undo + Redo are complementary, not redundant**: Redo ensures durability (forward), undo ensures atomicity and isolation (backward). You cannot have one without the other in a full ACID system.

2. **Clustered index is a double-edged sword**: Fast PK lookups and range scans, but secondary indexes pay a penalty. The choice of primary key is the most important schema decision in InnoDB.

3. **MVCC via undo logs is more space-efficient but more CPU-intensive**: No table bloat, but version reconstruction requires following pointers and applying undo records.

4. **The doublewrite buffer is a pragmatic solution**: It accepts a small performance cost for guaranteed crash safety. PostgreSQL's FPW approach is philosophically similar but integrated into WAL.

5. **InnoDB optimizes for OLTP**: The clustered index, buffer pool, and change buffer are all designed for transactional workloads with frequent small reads and writes.

6. **Long transactions are poison for InnoDB**: They prevent undo log purging, causing the undo tablespace to grow indefinitely and slowing down all reads.

---

## 10. References

- MySQL 8.0 Reference Manual - InnoDB: https://docs.oracle.com/cd/E17952_01/mysql-8.0-en/innodb-storage-engine.html
- "MySQL vs PostgreSQL Internals (Part 2) — MVCC": https://kernelmaker.github.io/mysql-vs-pg-mvcc
- "Atomicity/Durability — MTR and Redo Logs in InnoDB": https://medium.com/sys-base/mysql-day-19-durability-via-mtr-and-redo-logs-in-innodb-162d42a48668
- "Deep Dive: InnoDB Transactions and Write Paths": https://mariadb.org/wp-content/uploads/2018/07/Deep-Dive_-InnoDB-Transactions-and-Write-Paths.pdf
