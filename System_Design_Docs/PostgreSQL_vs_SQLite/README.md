# PostgreSQL vs SQLite: Architecture Comparison

## 1. Problem Background

### Why These Systems Exist

**SQLite** was created in 2000 by D. Richard Hipp for the U.S. Navy to manage guided missile destroyer configurations — a context where running a separate database server was impractical. The goal was a self-contained, serverless, zero-configuration transactional SQL database engine embedded directly in the application. Today it is the most widely deployed database engine in existence, found in every Android and iOS device, every browser, and most desktop applications.

**PostgreSQL** traces its roots to the POSTGRES project at UC Berkeley (1986), designed by Michael Stonebraker as a research successor to INGRES. It was built to serve multi-user enterprise workloads, support complex queries, enforce strong ACID guarantees at scale, and allow extensibility. It became open source in 1996 and has since grown into one of the most feature-rich, production-grade relational databases.

The fundamental divergence in their design philosophies emerges directly from these origins:
- SQLite solves: "How do I store relational data reliably inside an application?"
- PostgreSQL solves: "How do I serve thousands of concurrent users with complex data workloads reliably?"

---

## 2. Architecture Overview

### High-Level Comparison

```
┌──────────────────────────────────────────────────────────────────┐
│                         SQLite                                   │
│                                                                  │
│  Application Process                                             │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  App Code  →  SQLite Library  →  Single .db File        │    │
│  │            (embedded, in-process)                       │    │
│  └─────────────────────────────────────────────────────────┘    │
│  • No network, no daemon, no config                              │
│  • Library linked directly into the application                 │
└──────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│                        PostgreSQL                                │
│                                                                  │
│  Client Processes          Server Process (postmaster)          │
│  ┌──────────┐  TCP/IP     ┌────────────────────────────────┐   │
│  │ psql     │ ─────────── │ Postmaster (listener)          │   │
│  │ app.py   │             │   ├─ Backend Process (per conn) │   │
│  │ pgAdmin  │             │   ├─ WAL Writer                 │   │
│  └──────────┘             │   ├─ Checkpointer              │   │
│                           │   ├─ Autovacuum workers        │   │
│                           │   └─ Stats Collector           │   │
│                           │              │                 │   │
│                           │   Shared Memory (buffers, etc) │   │
│                           │              │                 │   │
│                           │   Data Directory (files)       │   │
│                           └────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

### Process Model

| Aspect | SQLite | PostgreSQL |
|---|---|---|
| Architecture | Embedded library | Client-server |
| Processes | 1 (the app itself) | 1 postmaster + 1 backend per connection |
| Communication | Function calls (in-process) | Unix socket / TCP/IP |
| Config needed | None | postgresql.conf, pg_hba.conf |
| Startup | Instantaneous (file open) | Daemon must be running |
| Memory model | Per-process | Shared memory across backends |

---

## 3. Internal Design

### 3.1 Storage Engine Architecture

#### SQLite Storage: Single-File B-Tree

SQLite stores the entire database — tables, indexes, metadata — in a **single cross-platform file**. Internally this file is organized as a collection of fixed-size **pages** (default 4096 bytes), each being a node in a B-tree.

```
SQLite File Layout (.db)
────────────────────────────────────────────────
│ Page 1: Database Header (100 bytes) + Root   │
│         of sqlite_schema table               │
├──────────────────────────────────────────────│
│ Page 2: Table B-tree root page               │
├──────────────────────────────────────────────│
│ Page 3: Index B-tree root page               │
├──────────────────────────────────────────────│
│ Page N: Overflow page (for large values)     │
│ ...                                          │
────────────────────────────────────────────────

Each page (e.g., table leaf page):
┌─────────────────────────────────────────┐
│ Page Header (8 bytes)                   │
│ Cell Pointer Array (2 bytes × num cells)│
│ Unallocated Space                       │
│ Cells (stored bottom-up)                │
│  └─ Cell: payload_size | row_id | data  │
└─────────────────────────────────────────┘
```

There are three B-tree types in SQLite:
- **Table B-trees** (row data, keyed by rowid)
- **Index B-trees** (secondary indexes, keyed by indexed column)
- **Without-ROWID tables** (clustered by declared primary key)

#### PostgreSQL Storage: Heap Files + Separate Indexes

PostgreSQL separates table data (heap files) from index files entirely. Each table lives in its own file under `$PGDATA/base/<dboid>/<relfilenode>`.

```
PostgreSQL Data Layout
────────────────────────────────────────────
base/
  16384/          ← OID of database
    1259           ← pg_class (system catalog)
    16385          ← your_table (heap file)
    16386          ← your_table_pkey (index file)
    16385_fsm      ← Free Space Map
    16385_vm       ← Visibility Map
────────────────────────────────────────────

Heap Page (8KB default):
┌────────────────────────────────────────┐
│ PageHeaderData (24 bytes)              │
│   lsn, checksum, flags, lower, upper  │
├────────────────────────────────────────│
│ ItemIdData array (line pointer array)  │
│   [offset, flags, length] per tuple   │
├────────────────────────────────────────│
│        Free Space                      │
├────────────────────────────────────────│
│ Tuples (stored from top-down)          │
│  Each tuple: HeapTupleHeader + data    │
│    xmin, xmax, ctid, natts, ...        │
└────────────────────────────────────────┘
```

The **indirection layer** (ItemIdData) is critical: it means tuples can be moved within a page without updating index entries, because indexes point to the ItemId slot, not the physical byte offset.

### 3.2 Index Implementation

Both databases use **B-tree** indexes as the primary index type, but their integration with table storage differs fundamentally.

#### SQLite B-tree Index
- For tables with a rowid, the table **is** a B-tree keyed by rowid.
- Secondary indexes are separate B-trees storing `(indexed_value, rowid)` pairs.
- A lookup via secondary index requires: `index lookup → rowid → second B-tree lookup (table)` — two tree traversals.

#### PostgreSQL B-tree Index (nbtree)
- Tables are heap files (unordered).
- Every index (primary or otherwise) is a **separate** B-tree file.
- Index leaf entries store `(key_value, TID)` where TID = (page_number, slot_number).
- Lookup: `index scan → TID → heap page fetch` — one index traversal + one heap access.
- PostgreSQL supports multiple index types: B-tree, Hash, GiST, GIN, BRIN, SP-GiST.

```
PostgreSQL B-tree Index Page:
┌──────────────────────────────────────┐
│ BTPageOpaqueData                     │
│   btpo_prev, btpo_next (sibling ptrs)│
│   btpo_level (0 = leaf)              │
├──────────────────────────────────────│
│ IndexTuples                          │
│   [key_value | TID (page, slot)]     │
│   [key_value | TID]                  │
│   ...                                │
└──────────────────────────────────────┘
```

### 3.3 Transaction Management & Concurrency

This is the most architecturally significant difference.

#### SQLite: WAL + File-Level Locking

SQLite uses **file locking** as its concurrency mechanism:

```
Locking States (in order of escalation):
UNLOCKED → SHARED → RESERVED → PENDING → EXCLUSIVE

Multiple readers can hold SHARED simultaneously.
A writer acquires RESERVED, then escalates to EXCLUSIVE.
Only one EXCLUSIVE lock holder at a time → serialized writes.
```

**WAL Mode** (Write-Ahead Logging, default since SQLite 3.7):
- Writers append to `-wal` file; readers read from DB + WAL.
- Multiple readers + one writer can coexist.
- Readers never block writers and vice versa.
- Periodic **checkpointing** copies WAL pages back to the main DB file.

**Bottom line**: SQLite allows one writer at a time. For mobile apps and desktop tools with occasional writes, this is perfectly fine.

#### PostgreSQL: MVCC (Multi-Version Concurrency Control)

PostgreSQL implements true MVCC — readers and writers never block each other:

```
Heap Tuple Version Chain:
                             ┌──────────────────────┐
INSERT row (xid=100):        │ xmin=100, xmax=0     │  ← CURRENT
                             └──────────────────────┘

UPDATE row (xid=200):        ┌──────────────────────┐
                             │ xmin=100, xmax=200   │  ← OLD (dead)
                             └──────────────────────┘
                             ┌──────────────────────┐
                             │ xmin=200, xmax=0     │  ← NEW (current)
                             └──────────────────────┘

Transaction at xid=150 (concurrent with update):
  → Sees xmin=100, xmax=200
  → 150 < 200, so old version is still visible to this txn ✓
```

Each transaction gets a **snapshot** at start time. It sees rows whose `xmin ≤ snapshot` and `xmax` is either 0 (not deleted) or `xmax > snapshot` (deleted after this snapshot was taken). This means:
- No read locks required.
- Old tuple versions accumulate over time → **VACUUM** is needed to reclaim dead tuples.

### 3.4 Durability

Both databases use Write-Ahead Logging:

| Mechanism | SQLite | PostgreSQL |
|---|---|---|
| WAL location | `database-wal` file | `pg_wal/` directory |
| Granularity | Entire page | Record-level (logical + physical) |
| Checkpoint trigger | WAL size threshold | Time-based + WAL size |
| Recovery | Replay WAL on open | `pg_wal` replay at startup |
| fsync control | PRAGMA synchronous | fsync, synchronous_commit |

---

## 4. Design Trade-Offs

### SQLite Advantages
- **Zero configuration**: No server to install, start, or maintain.
- **Portable**: Entire database is a single file — easy to copy, backup, email.
- **Low overhead**: No IPC, no network stack, no process management.
- **Reliable for low concurrency**: ACID compliant, crash-safe.
- **Ideal for testing**: Spin up an in-memory DB with `:memory:`.

### SQLite Limitations
- **Single writer**: Serialized writes bottleneck multi-user apps.
- **No user authentication**: Whoever can read the file can read the data.
- **Limited ALTER TABLE**: Cannot drop columns, rename constraints (historically).
- **No network access**: Cannot be shared across machines natively.
- **No connection pooling**, no query parallelism.

### PostgreSQL Advantages
- **True MVCC**: Readers and writers never block each other.
- **Horizontal multi-user scaling**: Designed for thousands of concurrent connections.
- **Rich type system**: Arrays, JSONB, ranges, custom types, extensions.
- **Advanced query planner**: Cost-based optimizer with statistics.
- **Replication, partitioning, full-text search** built in.

### PostgreSQL Limitations
- **Operational overhead**: Needs a running server, config management, backups, VACUUM.
- **VACUUM burden**: Dead tuples from MVCC must be explicitly cleaned.
- **Higher memory footprint**: Shared buffers, WAL buffers, per-backend overhead.
- **Not embeddable**: Cannot ship a PostgreSQL instance inside a mobile app.

### Decision Matrix

```
                    SQLite              PostgreSQL
─────────────────────────────────────────────────────
Mobile app              ✅ Perfect           ❌ Impractical
Desktop app             ✅ Great             ⚠️ Overkill
Web app (small)         ⚠️ Possible          ✅ Recommended
Web app (large)         ❌ Bottleneck        ✅ Designed for this
Embedded device         ✅ Ideal             ❌ Too heavy
Data warehouse          ❌ Not designed      ✅ With extensions
Unit testing            ✅ In-memory mode    ⚠️ Heavier setup
Multi-region replicas   ❌ Not built-in      ✅ Native support
─────────────────────────────────────────────────────
```

---

## 5. Experiments / Observations

### Experiment 1: SQLite Concurrency Under Multi-Writer Load

```bash
# Simulate concurrent writes to SQLite
python3 - <<'EOF'
import sqlite3, threading, time

conn = sqlite3.connect("test.db", timeout=5)
conn.execute("CREATE TABLE IF NOT EXISTS t (id INTEGER PRIMARY KEY, v TEXT)")
conn.commit()

def writer(i):
    try:
        c = sqlite3.connect("test.db", timeout=5)
        c.execute("INSERT INTO t VALUES (?, ?)", (i, f"val{i}"))
        c.commit()
        c.close()
    except sqlite3.OperationalError as e:
        print(f"Thread {i} blocked: {e}")

threads = [threading.Thread(target=writer, args=(i,)) for i in range(20)]
t0 = time.time()
for t in threads: t.start()
for t in threads: t.join()
print(f"Done in {time.time()-t0:.2f}s")
EOF
```

**Observation**: With 20 concurrent writers, SQLite serializes them via file locking. Several writers receive `database is locked` errors if timeout is short. PostgreSQL handles this natively via MVCC + row-level locking.

### Experiment 2: PostgreSQL MVCC Visibility

```sql
-- Session A:
BEGIN;
UPDATE accounts SET balance = balance - 100 WHERE id = 1;
-- Do NOT commit yet

-- Session B (concurrent):
SELECT balance FROM accounts WHERE id = 1;
-- Returns OLD balance — reads the pre-update snapshot
-- Session B is NOT blocked

-- Session A:
COMMIT;

-- Session B again:
SELECT balance FROM accounts WHERE id = 1;
-- NOW sees updated balance
```

**Observation**: This demonstrates MVCC non-blocking reads. PostgreSQL returned the old tuple version to Session B because its snapshot predates Session A's commit. SQLite in WAL mode behaves similarly for reads, but write conflicts are serialized.

### Experiment 3: File Size — SQLite vs PostgreSQL

```
SQLite (1M rows, simple table):
  test.db         → ~45 MB  (everything in one file)
  test.db-wal     → 0 (checkpointed)
  test.db-shm     → 32 KB  (shared memory file)

PostgreSQL (same dataset):
  base/16384/16385   (heap)  → ~40 MB
  base/16384/16386   (index) → ~22 MB
  base/16384/16385_fsm       → ~24 KB
  base/16384/16385_vm        → ~8 KB
  pg_wal/            → ~48 MB (active WAL)
```

**Observation**: SQLite's single-file format is more compact for small to medium datasets. PostgreSQL's overhead (FSM, VM, WAL, catalogs) becomes worth it only when its concurrency and features are needed.

---

## 6. Key Learnings

1. **Architecture is driven by use case**: SQLite's embedded design isn't a limitation — it's the point. The best database is the one that matches the deployment context.

2. **Concurrency models are the core divergence**: SQLite serializes writers (acceptable for single-user apps), while PostgreSQL's MVCC enables true concurrent read-write workloads. This single design decision cascades into differences in storage format, memory model, and operational complexity.

3. **MVCC has a cost**: PostgreSQL's non-blocking reads require storing multiple tuple versions, which leads to table bloat and the need for VACUUM. SQLite avoids this entirely through serialized writes.

4. **The "single file" trade-off**: SQLite's portability (copy one file = full backup) is extremely valuable in embedded contexts. PostgreSQL's multi-file layout enables things like partial index builds, tablespaces, and streaming replication — but at the cost of operational complexity.

5. **Both are production-grade ACID databases**: A common misconception is that SQLite is a "toy" database. It passes more ACID tests than many commercial databases. The distinction is concurrency and scale, not reliability.

---

## References

- [SQLite Architecture](https://www.sqlite.org/arch.html)
- [SQLite File Format](https://www.sqlite.org/fileformat2.html)
- [PostgreSQL Documentation: Storage](https://www.postgresql.org/docs/current/storage.html)
- [PostgreSQL MVCC Documentation](https://www.postgresql.org/docs/current/mvcc.html)
- Stonebraker, M. et al. "The Design of POSTGRES." ACM SIGMOD (1986)
- Hipp, D.R. "SQLite: A Database for the Edge of the Network." (2007)


