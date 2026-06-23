# PostgreSQL vs SQLite: Architecture Comparison

> **Author:** Pranay | Roll No: 24BCS10133  
> **Course:** Advanced DBMS — System Design Discussion

---

## 1. Problem Background

### Why do these two databases exist?

SQLite and PostgreSQL solve fundamentally different problems. Understanding *why* they were built the way they were is the key to understanding every architectural choice they make.

**SQLite** was created by D. Richard Hipp in 2000 for use on US Navy guided missile destroyers — machines that couldn't always run a full database server process. The design goal was a zero-configuration, serverless database that lives entirely inside the application. No setup, no daemon, no network. Just a library you link against. This philosophy permeates every design decision: single-file storage, no concurrent writers, no separate server process.

**PostgreSQL** has roots in the INGRES project at UC Berkeley (1986), later evolved as "Post-INGRES." Its design goal was the opposite: a full-featured, multi-user, enterprise-grade relational database that could serve hundreds of simultaneous clients with strong correctness guarantees. The entire architecture is oriented around shared state across many concurrent processes.

The core philosophical divergence: **SQLite optimizes for simplicity and embeddability; PostgreSQL optimizes for concurrency and correctness at scale.**

---

## 2. Architecture Overview

### High-Level Comparison

```
┌─────────────────────────────────────────────────────────────────────┐
│                         SQLite Architecture                         │
│                                                                     │
│   Application Process                                               │
│  ┌────────────────────────────────────────────────────────────┐    │
│  │  App Code  →  SQLite Library  →  Single .db File on Disk  │    │
│  │               (linked in)        (entire database = 1 file)│    │
│  └────────────────────────────────────────────────────────────┘    │
│                                                                     │
│   No network. No separate process. No config. Just a file.         │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                      PostgreSQL Architecture                        │
│                                                                     │
│  Client 1 ──┐                                                       │
│  Client 2 ──┤──► Postmaster ──► Backend Process 1 ──┐              │
│  Client 3 ──┘    (listener)     Backend Process 2 ──┤──► Shared    │
│                                 Backend Process 3 ──┘    Memory    │
│                                                          + Disk     │
│                                                                     │
│  Each connection = dedicated backend process                        │
│  All backends share: buffer pool, lock table, WAL buffers           │
└─────────────────────────────────────────────────────────────────────┘
```

### Component Comparison

| Component | SQLite | PostgreSQL |
|---|---|---|
| Process model | Embedded library | Client-server (process-per-connection) |
| Storage | Single file | Directory of files per database |
| Concurrency | Single writer / multiple readers | Full MVCC, many concurrent writers |
| Authentication | None (filesystem-level) | Role-based with pg_hba.conf |
| Networking | None | TCP/IP + Unix sockets |
| Extensions | Limited | Rich (PostGIS, pg_trgm, etc.) |
| WAL | Optional (WAL mode) | Always-on, central to durability |

---

## 3. Internal Design

### 3.1 Storage Engine Architecture

#### SQLite — B-tree over a Single File

SQLite's entire database is a single `.db` file divided into fixed-size **pages** (default: 4096 bytes). Every page is one of:

- **B-tree interior page** — stores keys and child page pointers
- **B-tree leaf page** — stores actual row data (for table pages) or index entries
- **Overflow page** — stores large values that don't fit in one page
- **Free page** — reclaimed space, tracked in a freelist

```
SQLite File Layout:
┌─────────────────────────────────────────────────────┐
│ Page 1 (Database Header — 100 bytes + B-tree root) │
├─────────────────────────────────────────────────────┤
│ Page 2  — sqlite_master (schema table root)         │
├─────────────────────────────────────────────────────┤
│ Page 3  — Table "users" root page                   │
├─────────────────────────────────────────────────────┤
│ Page 4  — Table "users" leaf page                   │
├─────────────────────────────────────────────────────┤
│ Page 5  — Index "idx_email" root page               │
├─────────────────────────────────────────────────────┤
│ ...                                                 │
└─────────────────────────────────────────────────────┘
```

**Key insight:** Every table in SQLite *is* a B-tree. Table rows are stored directly inside the B-tree leaf pages, keyed by `rowid`. This is called a **clustered index** by default — same concept as InnoDB's primary key clustering. Looking up a row by rowid = one B-tree traversal.

#### PostgreSQL — Heap Files + Separate Index Files

PostgreSQL separates concerns. Each table is a **heap file**: rows are appended in arrival order with no inherent sorting. Indexes are **separate files** that point back into the heap.

```
PostgreSQL Storage:
$PGDATA/base/<db_oid>/
    16384        ← heap file for table "users" (unordered rows)
    16384_fsm    ← Free Space Map (where to put new rows)
    16384_vm     ← Visibility Map (for VACUUM optimization)
    16389        ← B-tree index file for "idx_email"
```

**Why no clustering by default?** Because PostgreSQL's MVCC model creates multiple versions of the same row (old and new tuples coexist). Keeping them in a strict B-tree order would require expensive reorganization on every update. The heap model makes updates cheaper — just append the new version. The cost is that index scans require a "heap fetch" (jump from index to heap file), which adds I/O.

PostgreSQL does support `CLUSTER` and `CREATE INDEX ... USING ... WITH (fillfactor=...)` to physically reorder a heap — but it's a one-time operation, not maintained automatically.

### 3.2 Page Layout

#### SQLite Page Internals

```
SQLite B-tree Page (4096 bytes):
┌──────────────────────────────────────────────────────┐
│ Page Header (8 or 12 bytes)                          │
│  - Page type (leaf/interior)                         │
│  - First freeblock offset                            │
│  - Number of cells                                   │
│  - Cell content area start                           │
│  - Fragmented free bytes                             │
├──────────────────────────────────────────────────────┤
│ Cell Pointer Array                                   │
│  [offset1][offset2][offset3]...                      │
│  (2 bytes each, pointing to cell content below)      │
├──────────────────────────────────────────────────────┤
│ Unallocated space (grows toward cells from top)      │
├──────────────────────────────────────────────────────┤
│ Cell Content (stored from bottom up)                 │
│  Each cell: [payload size][rowid][payload data]      │
└──────────────────────────────────────────────────────┘
```

#### PostgreSQL Page Internals (8192 bytes default)

```
PostgreSQL Heap Page (8192 bytes):
┌──────────────────────────────────────────────────────┐
│ PageHeaderData (24 bytes)                            │
│  - pd_lsn: WAL LSN for this page                    │
│  - pd_lower: end of line pointer array               │
│  - pd_upper: start of tuple data                     │
│  - pd_special: start of special space                │
├──────────────────────────────────────────────────────┤
│ Line Pointer Array (ItemId[])                        │
│  Each = 4 bytes: [offset | flags | length]           │
│  Flags: LP_NORMAL, LP_REDIRECT, LP_DEAD              │
├──────────────────────────────────────────────────────┤
│ Free space                                           │
├──────────────────────────────────────────────────────┤
│ Tuple data (HeapTupleHeader + actual data)           │
│  Each tuple has:                                     │
│   - t_xmin: transaction that inserted this tuple     │
│   - t_xmax: transaction that deleted/updated it      │
│   - t_ctid: pointer to newest version of this tuple  │
│   - null bitmap + column data                        │
└──────────────────────────────────────────────────────┘
```

**Why does PostgreSQL store xmin/xmax in every tuple?** This is the foundation of MVCC. When a transaction updates a row, PostgreSQL doesn't overwrite the old tuple — it inserts a new one and sets `t_xmax` on the old one. Both versions coexist on the page. A reader checks its transaction snapshot against xmin/xmax to decide which version is "visible" to it. No locks needed for reads.

### 3.3 Concurrency Control

This is where the architectures diverge most dramatically.

#### SQLite Concurrency — File Locking

SQLite uses OS-level file locks. In the default journal mode:

```
Lock escalation sequence:
UNLOCKED → SHARED (read) → RESERVED (about to write) → PENDING → EXCLUSIVE (writing)
```

- Multiple readers can hold SHARED simultaneously
- Only **one** writer can exist at a time — it escalates to EXCLUSIVE
- While a writer holds EXCLUSIVE, all readers are blocked

**WAL mode** (Write-Ahead Logging, added in 3.7.0) changes this:

```
WAL mode:
- Writers append to WAL file (don't touch the main db file)
- Readers read from main db file + check WAL for newer versions
- Result: readers and one writer can coexist!
- Still: only ONE concurrent writer
```

WAL mode dramatically improves read concurrency for SQLite but the single-writer limitation remains fundamental. It's baked into the architecture.

#### PostgreSQL Concurrency — MVCC + Lock Manager

PostgreSQL implements full **Multiversion Concurrency Control (MVCC)**:

```
Transaction Timeline Example:
                    T1 (UPDATE users)    T2 (SELECT users)
Time ─────────────────────────────────────────────────────►
  t1: T1 begins (txid=500)
  t2:                          T2 begins (snapshot: see txids < 500)
  t3: T1 updates row A
      (old tuple: xmax=500, new tuple: xmin=500)
  t4:                          T2 reads row A
                               → sees old tuple (xmin < 500, xmax=500 not committed)
                               → T2 is NOT blocked
  t5: T1 commits
  t6:                          T2 still sees old tuple
                               (snapshot was taken at t2, before T1 committed)
  t7:                          T2 commits
  t8: New T3 begins → sees new tuple (T1 committed before T3's snapshot)
```

**Key insight:** PostgreSQL readers never block writers; writers never block readers. The cost is that dead tuples accumulate and must be cleaned by VACUUM.

PostgreSQL also has a full lock manager for DDL and explicit `SELECT FOR UPDATE`, with deadlock detection running every `deadlock_timeout` (default 1 second).

### 3.4 Transaction Management & Durability

#### SQLite Journaling

Two modes:
1. **Rollback journal** (default): Before modifying pages, write original pages to a `-journal` file. On crash, replay journal to undo partial writes.
2. **WAL mode**: Write changes to a separate WAL file. Main file only updated at checkpoints.

The rollback journal means SQLite must copy the *entire original page* before modifying it — even for a 1-byte change. This write amplification is acceptable for the scale SQLite targets.

#### PostgreSQL WAL

Every change in PostgreSQL generates a **WAL record** written to `pg_wal/`. WAL records are small, describing *what changed* (not entire pages). The sequence:

```
Write path:
1. Client sends UPDATE
2. Backend modifies page in shared buffer pool (in memory)
3. WAL record written to WAL buffer
4. On commit: WAL buffer flushed to disk (fsync)
5. Client receives success
6. Dirty buffer eventually written to heap file (async)

Crash recovery:
1. PostgreSQL starts, finds it crashed
2. Reads WAL from last checkpoint
3. Replays WAL records forward → heap files reach consistent state
4. Database is ready
```

**Why WAL before heap writes?** If PostgreSQL wrote to the heap file first and crashed, the heap would be inconsistent with no way to reconstruct the lost data. WAL ensures that even if the heap file is partially written, the WAL can restore it. This is the **Write-Ahead Logging** guarantee: WAL is always durable before the user gets a success response.

---

## 4. Design Trade-Offs

### SQLite Trade-Offs

| Decision | Advantage | Limitation |
|---|---|---|
| Single file | Zero setup, trivially portable, easy backup | Can't span disks, size limits (~281 TB theoretical but ~1 TB practical) |
| Single writer | Simple implementation, no distributed locking | Terrible for write-heavy multi-user workloads |
| Embedded | No network overhead, no process boundary | Can't be shared across machines or processes (easily) |
| No user auth | No configuration needed | Security entirely depends on filesystem permissions |
| Loose typing | Flexible, good for dynamic apps | Can lead to subtle data integrity bugs (`SELECT '5' = 5` returns true) |
| Serverless | No DBA needed | No tuning knobs, hard to optimize for specific workloads |

### PostgreSQL Trade-Offs

| Decision | Advantage | Limitation |
|---|---|---|
| Process-per-connection | Isolation — crash of one connection can't corrupt others | High memory overhead (~5–10MB per connection); requires pgBouncer at scale |
| MVCC with dead tuples | Readers never block writers | Dead tuples accumulate; VACUUM required; write amplification on tables with heavy updates |
| Separate heap + index | Index scans are flexible (can use any column) | Index scan + heap fetch = 2 I/Os vs SQLite's 1 for rowid lookup |
| Always-on WAL | Strong durability guarantees | WAL generates significant I/O; `synchronous_commit = off` trades safety for speed |
| Rich type system | Enforces data integrity | Stricter than SQLite; migration requires explicit type handling |
| Client-server separation | Accessible from any machine on the network | Requires network setup, authentication configuration |

### The Fundamental Trade-Off

```
SQLite world:                     PostgreSQL world:
───────────────────────────       ────────────────────────────────
Optimizes for:                    Optimizes for:
  Single user                       Many concurrent users
  Single machine                    Distributed clients
  Simple deployment                 Complex workloads
  Read-heavy                        Mixed read/write
  Small to medium data              Large data, high throughput

Accepts:                          Accepts:
  Single writer bottleneck          VACUUM complexity
  Limited SQL feature set           Higher operational overhead
  No network access                 More tuning required
  No authentication                 Process memory overhead
```

---

## 5. Experiments / Observations

### Experiment 1: Observing SQLite's Single-Writer Bottleneck

```python
import sqlite3
import threading
import time

def writer(db_path, thread_id):
    conn = sqlite3.connect(db_path, timeout=5)
    try:
        for i in range(100):
            conn.execute(f"INSERT INTO test VALUES ({thread_id * 1000 + i})")
            conn.commit()
    except sqlite3.OperationalError as e:
        print(f"Thread {thread_id} failed: {e}")  # "database is locked"
    finally:
        conn.close()

# With 5 concurrent writer threads → many "database is locked" errors
# With WAL mode: conn.execute("PRAGMA journal_mode=WAL") → fewer but still some
```

**Observation:** In default journal mode, concurrent writes immediately hit locking errors. WAL mode reduces failures but doesn't eliminate the single-writer constraint.

### Experiment 2: PostgreSQL EXPLAIN ANALYZE showing MVCC Overhead

```sql
-- After many updates to a table, check dead tuple count
SELECT relname, n_live_tup, n_dead_tup, 
       round(n_dead_tup::numeric/NULLIF(n_live_tup+n_dead_tup,0)*100, 2) AS dead_pct
FROM pg_stat_user_tables
WHERE relname = 'users';

-- Typical result after 10k updates without VACUUM:
-- relname | n_live_tup | n_dead_tup | dead_pct
-- users   |   10000    |   10000    |  50.00

-- After VACUUM:
-- users   |   10000    |      0     |   0.00
```

**Observation:** MVCC's "append new version" strategy leaves dead tuples. VACUUM is not optional — it's the cleanup mechanism that MVCC fundamentally requires.

### Experiment 3: Page Size Impact

```bash
# SQLite with different page sizes (must be set before any data)
sqlite3 test_4k.db "PRAGMA page_size=4096; CREATE TABLE t(x TEXT);"
sqlite3 test_16k.db "PRAGMA page_size=16384; CREATE TABLE t(x TEXT);"

# Insert 100k rows, compare file sizes and query times
# Larger pages → fewer page reads for sequential scans
# Larger pages → more wasted space for small tables
# PostgreSQL hardcoded at 8192 (configurable at compile time only)
```

**Observation:** SQLite's tunable page size gives it flexibility that PostgreSQL doesn't have at runtime. PostgreSQL's fixed 8KB is a deliberate choice — it's the sweet spot for mixed workloads on most OS page cache sizes.

---

## 6. Key Learnings

**1. Architecture follows use case, not the other way around.**  
SQLite's single-file, single-writer design isn't a limitation — it's a deliberate fit for its target environment (mobile apps, embedded systems, testing). PostgreSQL's process-per-connection model isn't wasteful — it's the right choice for isolation in a multi-tenant server.

**2. Concurrency is the hardest problem in database design.**  
SQLite avoids it by saying "only one writer." PostgreSQL solves it with MVCC, but MVCC requires VACUUM. Every design choice creates a ripple of consequences.

**3. The heap vs clustered-index trade-off has no winner.**  
PostgreSQL's heap model makes MVCC cleaner but requires a heap fetch after each index scan. SQLite's (and InnoDB's) clustered approach makes primary key lookups extremely fast but complicates MVCC and secondary index updates.

**4. WAL is the universal durability mechanism.**  
Both databases (in their "safe" modes) use write-ahead logging. The difference is *what* goes in the WAL — SQLite writes entire original pages; PostgreSQL writes compact change records. This reflects their different scale targets.

**5. "Simple" hides complexity.**  
SQLite looks trivially simple from the outside (just a file!) but its internal B-tree management, page format, and locking protocol are non-trivial. Simplicity at the interface level doesn't mean simplicity at the implementation level.

---

## References

- SQLite internals: https://www.sqlite.org/fileformat2.html  
- PostgreSQL storage: https://www.postgresql.org/docs/current/storage.html  
- PostgreSQL MVCC: https://www.postgresql.org/docs/current/mvcc.html  
- D. Richard Hipp — SQLite design decisions talk (2016)  
- "Architecture of a Database System" — Hellerstein, Stonebraker, Hamilton (2007)
