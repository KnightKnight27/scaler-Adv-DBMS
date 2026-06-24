# PostgreSQL vs SQLite3: Architectural Design Comparison

## Overview

This document compares the internal architectures of PostgreSQL and SQLite3, two widely-used relational databases with fundamentally different design philosophies.

| Aspect | PostgreSQL | SQLite3 |
|--------|-----------|---------|
| **Model** | Client-server (multi-process) | Embedded (in-process library) |
| **Concurrency** | MVCC with full isolation | File-level locking / WAL |
| **Storage** | Heap files + TOAST | Single B-Tree file |
| **Use Case** | Enterprise, multi-user | Embedded, single-user, mobile |

---

## 1. Process Architecture

### PostgreSQL: Multi-Process Model
```
┌──────────────────────────────────────────────────────────────┐
│                     PostgreSQL Server                         │
│                                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │   Postmaster  │  │  Background  │  │   WAL Writer │       │
│  │   (main pid)  │  │   Writer     │  │              │       │
│  └──────┬───────┘  └──────────────┘  └──────────────┘       │
│         │                                                     │
│  ┌──────▼───────┐  ┌──────────────┐  ┌──────────────┐       │
│  │  Backend #1   │  │  Backend #2   │  │  Autovacuum  │       │
│  │  (client conn)│  │  (client conn)│  │  Worker      │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
│                                                               │
│  ┌──────────────────────────────────────────────────┐        │
│  │           Shared Memory (shared_buffers)          │        │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌────────┐ │        │
│  │  │ Buffer  │ │  WAL    │ │  Lock   │ │ CLOG   │ │        │
│  │  │  Pool   │ │ Buffers │ │  Table  │ │        │ │        │
│  │  └─────────┘ └─────────┘ └─────────┘ └────────┘ │        │
│  └──────────────────────────────────────────────────┘        │
└──────────────────────────────────────────────────────────────┘
```

- **Postmaster** forks a new backend process for each client connection
- All backends share memory via `shared_buffers`
- Background processes: bgwriter, WAL writer, autovacuum, checkpointer
- IPC via shared memory and signals

### SQLite3: Serverless / Embedded Model
```
┌──────────────────────────────────────────────────────────────┐
│                  Application Process                          │
│                                                               │
│  ┌──────────────────────────────────────────────────┐        │
│  │              SQLite3 Library (libsqlite3)         │        │
│  │                                                    │        │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────┐   │        │
│  │  │ SQL      │  │ B-Tree   │  │ Page Cache    │   │        │
│  │  │ Compiler │  │ Module   │  │ (per-conn)    │   │        │
│  │  └──────────┘  └──────────┘  └──────┬───────┘   │        │
│  │                                      │            │        │
│  │                              ┌───────▼───────┐   │        │
│  │                              │  OS Interface  │   │        │
│  │                              │  (VFS layer)   │   │        │
│  │                              └───────┬───────┘   │        │
│  └──────────────────────────────────────┼───────────┘        │
└─────────────────────────────────────────┼────────────────────┘
                                          │
                                  ┌───────▼───────┐
                                  │  Database File │
                                  │  (single file) │
                                  └───────────────┘
```

- No separate server process — linked directly into the application
- Each connection has its own page cache (no shared memory)
- Concurrency control via file locks (fcntl) or WAL
- VFS layer abstracts OS-specific file operations

---

## 2. Storage Engine

### PostgreSQL: Heap Storage + TOAST

```
Database Cluster ($PGDATA/)
├── base/
│   └── <oid>/              ← one directory per database
│       ├── 16384           ← heap file for a table (8KB pages)
│       ├── 16384_fsm       ← free space map
│       ├── 16384_vm        ← visibility map
│       └── 16385           ← another table/index
├── pg_wal/                 ← WAL segment files (16MB each)
├── pg_clog/                ← commit log (transaction status)
└── pg_xact/                ← transaction status (pg 10+)
```

- **Heap files**: Unordered collection of tuples in 8KB pages
- **TOAST**: Large values (>2KB) are compressed and/or stored out-of-line
- **Indexes**: Separate B-Tree files that point back to heap via TID (tuple ID)
- **Page format**: header → item pointers → free space → tuples (grows inward)

### SQLite3: Single-File B-Tree Storage

```
Database File (*.db)
┌─────────────────────────────────────┐
│ Page 1: File Header + sqlite_master │  ← schema table (root page)
├─────────────────────────────────────┤
│ Page 2: Table B-Tree root           │  ← interior or leaf node
├─────────────────────────────────────┤
│ Page 3: Index B-Tree root           │
├─────────────────────────────────────┤
│ Page 4: B-Tree leaf page            │
├─────────────────────────────────────┤
│ ...                                 │
├─────────────────────────────────────┤
│ Page N: Overflow page               │  ← for large records
└─────────────────────────────────────┘
```

- **Single file**: Entire database in one file (portable!)
- **Page types**: Interior nodes, leaf nodes, overflow pages, freelist pages
- **B-Tree**: Tables use B*-Tree (data in leaves), indexes use B-Tree
- **Default page size**: 4096 bytes (configurable: 512 to 65536)

---

## 3. Concurrency Control

### PostgreSQL: Full MVCC

| Feature | Details |
|---------|---------|
| **Mechanism** | Multi-Version Concurrency Control |
| **Versioning** | Each row has `xmin` (created by) and `xmax` (deleted by) transaction IDs |
| **Snapshots** | Each transaction gets a consistent snapshot at `BEGIN` |
| **Isolation Levels** | READ COMMITTED, REPEATABLE READ, SERIALIZABLE |
| **Readers vs Writers** | Readers NEVER block writers, writers NEVER block readers |
| **Vacuum** | Dead tuples must be cleaned up by VACUUM |

```sql
-- Example: Two concurrent transactions see consistent data
-- Transaction 1                    -- Transaction 2
BEGIN;                              BEGIN;
UPDATE accounts SET balance = 100   SELECT balance FROM accounts;
WHERE id = 1;                       -- Sees OLD value (MVCC snapshot)
COMMIT;                             SELECT balance FROM accounts;
                                    -- Still sees old value (snapshot)
                                    COMMIT;
```

### SQLite3: File Locking / WAL

| Feature | Rollback Journal Mode | WAL Mode |
|---------|----------------------|----------|
| **Readers block writers** | Yes | No |
| **Writers block readers** | Yes | No |
| **Max concurrent writers** | 1 | 1 |
| **Max concurrent readers** | Many (if no writer) | Many (even with writer) |
| **Durability** | Journal file | WAL file |

```
Lock Escalation (Rollback Journal Mode):
  UNLOCKED → SHARED → RESERVED → PENDING → EXCLUSIVE
                ↑                              ↑
           (for reads)                    (for writes)
```

---

## 4. Write-Ahead Logging (WAL)

### PostgreSQL WAL
- **Segment files**: 16MB segments in `pg_wal/`
- **LSN**: Log Sequence Number identifies each WAL record
- **Synchronous commit**: `fsync()` WAL on every commit (configurable)
- **Replication**: WAL is streamed to replicas for physical replication
- **Recovery**: Replay WAL from last checkpoint to recover from crash
- **Full-page writes**: First modification after checkpoint writes entire page

### SQLite WAL
- **Single file**: `database.db-wal` alongside the main database
- **Shared memory**: `database.db-shm` for WAL index (frame lookup)
- **Checkpoint**: Copies committed WAL frames back to main database
- **Auto-checkpoint**: Default at 1000 pages
- **No replication**: WAL is local only

---

## 5. Buffer Pool / Page Cache

### PostgreSQL: Shared Buffer Pool
```
shared_buffers (configurable, default 128MB)
┌──────────────────────────────────────────────────┐
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐   │
│  │ Tag:   │ │ Tag:   │ │ Tag:   │ │ Tag:   │   │
│  │ rel=1  │ │ rel=1  │ │ rel=2  │ │ rel=3  │   │
│  │ blk=5  │ │ blk=12 │ │ blk=0  │ │ blk=7  │   │
│  │        │ │        │ │        │ │        │   │
│  │ pin: 2 │ │ pin: 0 │ │ pin: 1 │ │ pin: 0 │   │
│  │ dirty  │ │ clean  │ │ dirty  │ │ clean  │   │
│  │ usage:3│ │ usage:1│ │ usage:2│ │ usage:0│   │
│  └────────┘ └────────┘ └────────┘ └────────┘   │
│                                                  │
│  Eviction: Clock-Sweep algorithm                 │
│  Clock hand → scans buffer, decrements usage     │
│  Evicts when usage_count reaches 0 and pin = 0   │
└──────────────────────────────────────────────────┘
```

- Shared across all backends (processes)
- **Clock-Sweep** eviction (not LRU!) — see Lab 3
- Each frame: buffer tag, pin count, usage count, dirty flag
- Background writer flushes dirty pages periodically

### SQLite: Per-Connection Page Cache
- Each connection has its own private page cache
- Default cache size: 2000 pages (-2000 = 2000 pages)
- LRU eviction policy
- No sharing between connections (no shared memory in default mode)
- `PRAGMA cache_size` to configure

---

## 6. Indexing

| Feature | PostgreSQL | SQLite3 |
|---------|-----------|---------|
| **Default index** | B-Tree | B-Tree |
| **Other types** | Hash, GiST, SP-GiST, GIN, BRIN | None (B-Tree only) |
| **Clustered index** | No (heap storage, CLUSTER is one-time) | Yes (INTEGER PRIMARY KEY = rowid) |
| **Covering index** | Yes (INCLUDE clause) | Yes (implicit for single-col) |
| **Partial index** | Yes (WHERE clause) | Yes (WHERE clause) |
| **Expression index** | Yes | Yes |

### Key Difference: Clustered vs Heap

**SQLite (Clustered)**:
```
B-Tree leaf contains: [rowid → full row data]
Index lookup: Index B-Tree → rowid → Table B-Tree → row data
WITHOUT ROWID tables: clustered on PRIMARY KEY
```

**PostgreSQL (Heap)**:
```
Heap page contains: [tuple data at arbitrary position]
Index lookup: Index B-Tree → TID (page#, offset) → Heap page → tuple
CLUSTER: one-time physical reordering (not maintained)
```

---

## 7. Query Processing

### PostgreSQL Query Pipeline
```
SQL Text
  │
  ▼
Parser (gram.y) → Parse Tree
  │
  ▼
Analyzer → Query Tree (resolved names, types)
  │
  ▼
Rewriter → Rewritten Query (views, rules expanded)
  │
  ▼
Planner/Optimizer → Plan Tree (cost-based, dynamic programming)
  │                  ├── Sequential Scan
  │                  ├── Index Scan / Index Only Scan
  │                  ├── Bitmap Scan
  │                  ├── Nested Loop / Hash Join / Merge Join
  │                  └── Sort / Aggregate / Limit
  ▼
Executor → Result Tuples
```

### SQLite Query Pipeline
```
SQL Text
  │
  ▼
Tokenizer → Tokens
  │
  ▼
Parser (Lemon) → Parse Tree
  │
  ▼
Code Generator → VDBE Bytecode
  │               ├── OpenRead (open B-Tree cursor)
  │               ├── Rewind / Next (iterate rows)
  │               ├── Column (extract column)
  │               ├── Compare / Jump (WHERE filter)
  │               └── ResultRow (output)
  ▼
VDBE (Virtual Database Engine) → Executes bytecode
```

**Key difference**: PostgreSQL uses a cost-based optimizer with statistics. SQLite uses a simpler rule-based optimizer (though it's gotten more sophisticated over time with the Next-Generation Query Planner — NGQP).

---

## 8. Summary: When to Use Which?

| Use Case | Choose |
|----------|--------|
| Multi-user web application | **PostgreSQL** |
| Mobile app local storage | **SQLite** |
| IoT / edge computing | **SQLite** |
| Complex queries, analytics | **PostgreSQL** |
| Embedded systems | **SQLite** |
| Replication / HA required | **PostgreSQL** |
| Testing / prototyping | **SQLite** |
| Configuration / cache storage | **SQLite** |
| Enterprise data warehouse | **PostgreSQL** |
| Desktop application | **SQLite** |

## References

- [SQLite File Format](https://www.sqlite.org/fileformat2.html)
- [SQLite WAL Mode](https://www.sqlite.org/wal.html)
- [PostgreSQL Internals (interdb.jp)](https://www.interdb.jp/pg/)
- [PostgreSQL Buffer Manager](https://www.postgresql.org/docs/current/storage-buffer.html)
