# PostgreSQL vs SQLite: Architecture Comparison

## Overview

This document analyzes and compares the internal architecture of PostgreSQL and SQLite — two widely-used relational database systems that represent fundamentally different design philosophies. PostgreSQL is a full-featured client-server database built for concurrent, multi-user enterprise environments. SQLite is an embedded, serverless database designed to be a lightweight, self-contained component within an application. Understanding *why* these design differences exist reveals deeper truths about how database engineers think about trade-offs between concurrency, portability, simplicity, and scale.

---

## 1. Overall Architecture

### PostgreSQL — Client-Server Model

PostgreSQL follows a **process-per-connection** client-server architecture.

```
┌────────────────────────────────────────────────────┐
│                   Client Application               │
└───────────────────────┬────────────────────────────┘
                        │ TCP/IP or Unix Socket
                        ▼
┌────────────────────────────────────────────────────┐
│              Postmaster (Listener Process)          │
│         Accepts connections, forks backends         │
└───────────────────────┬────────────────────────────┘
          ┌─────────────┼─────────────┐
          ▼             ▼             ▼
    [Backend 1]   [Backend 2]   [Backend N]   ← one OS process per connection
          │             │             │
          └─────────────┼─────────────┘
                        ▼
        ┌───────────────────────────────┐
        │        Shared Memory          │
        │  Shared Buffer Pool (BGWriter)│
        │  WAL Buffers                  │
        │  Lock Tables                  │
        └───────────────────────────────┘
                        │
                        ▼
               [Data Files on Disk]
```

**Why this design?**
- Isolation: If one client crashes, only its backend process dies — the server keeps running.
- Security: Each process runs with OS-level isolation, preventing one user's query from corrupting another's memory.
- Multi-user: Dozens or hundreds of clients can run queries simultaneously via shared buffer pool and locking.

### SQLite — Embedded / Serverless Model

SQLite is a **library** that is linked directly into the application process. There is no separate server process.

```
┌──────────────────────────────────────────────────┐
│                Application Process               │
│                                                  │
│   ┌──────────────────────────────────────────┐  │
│   │           SQLite Library (.so/.dll)       │  │
│   │  ┌────────────┐   ┌───────────────────┐  │  │
│   │  │  SQL Parser│   │  B-Tree Engine    │  │  │
│   │  └────────────┘   └───────────────────┘  │  │
│   │  ┌────────────────────────────────────┐  │  │
│   │  │        Page Cache (in-process)     │  │  │
│   │  └────────────────────────────────────┘  │  │
│   └──────────────────────────────────────────┘  │
│                        │                         │
└────────────────────────┼─────────────────────────┘
                         │ read/write
                         ▼
                 [Single .db File]
```

**Why this design?**
- Zero configuration: No installation, no port, no credentials — just a file.
- Portability: A single `.db` file can be moved, copied, or backed up trivially.
- Low overhead: No network stack, no IPC, no process management — ideal for mobile apps, browsers, IoT devices.

---

## 2. Process Model

| Aspect | PostgreSQL | SQLite |
|---|---|---|
| Architecture | Multi-process (postmaster + backends) | Single-process library |
| Connection model | One OS process per client | Called directly in-process |
| Memory isolation | Per-process heap + shared memory | Single shared heap |
| Crash resilience | One client crash ≠ server crash | App crash = database access ends |
| Network | TCP/IP or Unix socket | Not applicable |

PostgreSQL's process-per-connection model was chosen over threads because threads share a memory space — a bug in one thread can corrupt data for all others. Using processes avoids this at the cost of higher resource usage. (PgBouncer is often used to pool connections and reduce process overhead.)

SQLite runs in-process because it's designed to be an embedded component — a replacement for flat files in a single application, not a multi-user server.

---

## 3. Storage Engine Architecture

### PostgreSQL Storage Engine

PostgreSQL uses a **heap-based storage engine** with separate index structures.

- **Heap files**: Tables are stored as unordered heaps of variable-length tuples.
- **TOAST** (The Oversized-Attribute Storage Technique): Large values (text, JSONB, bytea) are stored out-of-line in a separate TOAST table, transparently.
- **Free Space Map (FSM)**: Tracks available space in heap pages for INSERT reuse.
- **Visibility Map (VM)**: Tracks pages with only live tuples (used by VACUUM and index-only scans).

PostgreSQL's MVCC (see Section 6) writes new tuple versions directly into the heap. Old versions (dead tuples) accumulate until `VACUUM` reclaims them. This design favors write throughput at the cost of requiring periodic maintenance.

### SQLite Storage Engine

SQLite uses a **unified B-tree storage engine** where everything — tables and indexes alike — is stored as B-trees within a single file.

- **Tables** are stored as B-trees keyed by `rowid` (an implicit 64-bit integer primary key).
- **WITHOUT ROWID tables** (added later) use a clustered B-tree on the primary key.
- **Indexes** are separate B-trees in the same file.

This unified B-tree approach simplifies the storage layer dramatically. With no heap file, no FSM, and no TOAST-equivalent, SQLite's entire storage model fits in a few thousand lines of C.

---

## 4. Database File Organization

### PostgreSQL

PostgreSQL stores data in a **data directory** (`PGDATA`) with a structured hierarchy:

```
$PGDATA/
├── base/           ← per-database directories
│   └── 16384/      ← one folder per database (OID)
│       ├── 1259    ← heap file for pg_class
│       ├── 1259_fsm← free space map
│       ├── 1259_vm ← visibility map
│       └── ...
├── global/         ← cluster-wide tables (pg_database, etc.)
├── pg_wal/         ← Write-Ahead Log segments
├── pg_xact/        ← transaction status (commit log)
└── postgresql.conf
```

Each table and index gets **its own file** (or set of files if it exceeds 1 GB). This makes individual table management easy but means a busy PostgreSQL instance can have thousands of files on disk.

### SQLite

SQLite stores **everything in a single file**:

```
myapp.db        ← entire database: all tables, all indexes, all metadata
myapp.db-wal    ← WAL file (only present during WAL mode transactions)
myapp.db-shm    ← shared memory index for WAL (only in WAL mode)
```

The single-file design is a core SQLite feature. It allows atomic database operations (just move the file), trivial backup (just copy the file), and trivial deployment (no directory structure). The trade-off is that very large databases become a single large file that's harder to manage at the OS level.

---

## 5. Page/Block Layout

Both databases organize storage into fixed-size **pages (blocks)**.

### PostgreSQL Page Layout (default 8 KB)

```
┌─────────────────────────────────────────┐  ← 8192 bytes
│  Page Header (24 bytes)                 │
│  (LSN, checksum, flags, free space ptrs)│
├─────────────────────────────────────────┤
│  Item ID Array (4 bytes per tuple ptr)  │
│  grows →                                │
├─────────────────────────────────────────┤
│                                         │
│         Free Space                      │
│                                         │
├─────────────────────────────────────────┤
│  Tuple Data                  ← grows    │
│  (HeapTupleHeader + actual data)        │
└─────────────────────────────────────────┘
```

Each tuple has a **HeapTupleHeader** containing system columns: `xmin` (inserting transaction), `xmax` (deleting transaction), `ctid` (physical location), `cmin/cmax` (command IDs). These are essential for MVCC visibility checks.

### SQLite Page Layout (default 4 KB, configurable)

SQLite pages are B-tree nodes and come in two types:

```
┌─────────────────────────────────────────┐  ← 4096 bytes (default)
│  Page Header (8 or 12 bytes)            │
│  (page type, first freeblock, cell count│
│   cell content start, fragmented bytes) │
├─────────────────────────────────────────┤
│  Cell Pointer Array (2 bytes per cell)  │
│  grows →                                │
├─────────────────────────────────────────┤
│                                         │
│         Unallocated Space               │
│                                         │
├─────────────────────────────────────────┤
│  Cell Content Area               ← grows│
│  (key + data packed into cells)         │
└─────────────────────────────────────────┘
```

SQLite pages are **always B-tree nodes** — interior nodes (keys + child pointers) or leaf nodes (keys + data). There is no heap structure.

---

## 6. Index Implementation

### PostgreSQL Indexes

PostgreSQL supports **multiple index types**:

| Index Type | Use Case | Structure |
|---|---|---|
| B-Tree (default) | Equality, range queries, sorting | Balanced tree |
| Hash | Equality only | Hash table |
| GIN | Full-text search, arrays, JSONB | Inverted index |
| GiST | Geometric, range types | Generalized search tree |
| BRIN | Large append-only tables | Block range summaries |

All indexes in PostgreSQL are **separate files** from the heap. A B-tree index entry contains `(key, heap_tid)` — the key value and the physical location of the tuple in the heap. This is a **secondary index** pattern: the index points to the heap, and a heap fetch is needed to get the full row.

**Index-only scans** (available when the Visibility Map shows a page is all-live) avoid the heap fetch.

### SQLite Indexes

SQLite uses **B-tree indexes** exclusively, stored within the same database file as the tables.

- For **rowid tables**: An index entry contains `(key, rowid)`. Finding a row requires two B-tree lookups: one in the index, then one in the table B-tree by rowid. Similar to PostgreSQL's secondary index + heap fetch.
- For **WITHOUT ROWID tables**: The primary key B-tree stores the full row (clustered). Secondary indexes store `(key, primary_key)` and require a lookup in the primary B-tree.

SQLite does not support Hash, GIN, GiST, or BRIN indexes. For full-text search, SQLite provides FTS5 as a virtual table extension — a separate mechanism altogether.

---

## 7. Transaction Management & Concurrency Control

This is where PostgreSQL and SQLite diverge most significantly, and understanding this difference explains most of their real-world use-case differences.

### PostgreSQL — MVCC (Multi-Version Concurrency Control)

PostgreSQL implements **full MVCC** at the tuple level. Every row has two transaction ID stamps:
- `xmin`: The transaction that inserted this row version.
- `xmax`: The transaction that deleted (or updated) this row version (0 if still live).

When a transaction reads a row, PostgreSQL checks whether the row's `xmin`/`xmax` are visible to its **snapshot** (the set of committed transactions at the moment the transaction started).

**Key MVCC properties:**
- **Readers never block writers**: A SELECT never waits for concurrent INSERTs/UPDATEs.
- **Writers never block readers**: An UPDATE/INSERT never waits for concurrent SELECTs.
- **Snapshot Isolation**: Each transaction sees a consistent point-in-time view of the database.
- **Cost**: Dead tuples accumulate and must be cleaned by `VACUUM`. Long-running transactions prevent cleanup.

```
Time →
Transaction A (xid=100): INSERT row (xmin=100)
Transaction B (xid=101): UPDATE row → new version (xmin=101, xmax=0), old version (xmin=100, xmax=101)
Transaction C (xid=50, snapshot before 100): still sees old version — xmin=100 not visible
Transaction D (xid=102, snapshot after 101): sees new version — both transactions committed
```

For **write conflicts**, PostgreSQL uses **row-level locking**: only conflicting writes to the same row block each other.

### SQLite — File-Level Locking + Optional WAL

SQLite's concurrency model is fundamentally simpler because it was designed for single-writer scenarios.

**Classic journal mode (rollback journal):**

SQLite uses **file-level locks** with five states:
1. `UNLOCKED` — no lock held
2. `SHARED` — multiple readers can hold this simultaneously
3. `RESERVED` — one writer preparing to write (readers still allowed)
4. `PENDING` — writer waiting for existing readers to finish
5. `EXCLUSIVE` — writer has sole access; all readers blocked

This means **only one writer can exist at a time**, and during a write, eventually all readers are blocked. This is a severe concurrency limitation for multi-user scenarios.

**WAL mode (Write-Ahead Logging, added in 3.7.0):**

WAL mode significantly improves SQLite's concurrency:
- Readers read the database file and consult the WAL for newer versions.
- One writer can write to the WAL while readers continue reading the database file.
- **Writers and readers no longer block each other** (similar to PostgreSQL MVCC).
- **Only one writer at a time** is still enforced.

```
SQLite WAL Mode:
┌──────────────┐        ┌─────────────┐
│ DB File      │        │  WAL File   │
│ (older data) │        │ (new pages) │
└──────────────┘        └─────────────┘
     ↑                        ↑
  Readers (see both,      Writer (appends
  merge in memory)         new pages here)
```

**Comparison summary:**

| Feature | PostgreSQL | SQLite (WAL) |
|---|---|---|
| Concurrent readers | ✅ Unlimited | ✅ Unlimited |
| Concurrent writers | ✅ Row-level locking | ❌ Single writer |
| Read/write concurrency | ✅ Non-blocking | ✅ Non-blocking (WAL) |
| MVCC | ✅ Full tuple-level | ❌ None |
| Isolation levels | Read Committed, Repeatable Read, Serializable | Serializable only |

---

## 8. Durability Mechanisms

### PostgreSQL — WAL (Write-Ahead Logging)

PostgreSQL uses WAL for crash recovery and durability:

1. Before any data page is modified, a **WAL record** describing the change is written to WAL buffers.
2. On transaction commit, WAL buffers are flushed to disk (`fsync`).
3. Data pages in the shared buffer pool are written lazily by the **background writer** and **checkpointer**.
4. On crash, PostgreSQL replays WAL from the last checkpoint to recover all committed transactions.

WAL also enables **streaming replication** (sending WAL records to standby servers) and **point-in-time recovery (PITR)**.

**Checkpoint process**: Periodically, PostgreSQL writes all dirty buffer pages to disk and records a checkpoint in the WAL. Recovery only needs to replay WAL from the most recent checkpoint.

### SQLite — Rollback Journal and WAL

SQLite provides two durability modes:

**Rollback Journal** (default):
1. Before modifying a page, SQLite writes the **original page** to a journal file.
2. On commit, the journal is deleted (or zeroed).
3. On crash, SQLite checks if a journal exists — if so, restores original pages from it.
4. This is an **undo log** (journal contains before-images), unlike PostgreSQL's redo log.

**WAL mode**:
1. Changes are written as new pages to the WAL file.
2. The database file is only updated during a **checkpoint** operation.
3. On crash, uncommitted WAL pages are ignored; the database file is consistent.
4. This is a **redo log** (WAL contains after-images), similar to PostgreSQL.

SQLite's `PRAGMA synchronous` controls how aggressively it calls `fsync`:
- `FULL`: fsync after each write (safest, slowest)
- `NORMAL`: fsync less frequently (slight risk)
- `OFF`: no fsync (fast, but data can be lost on OS crash)

---

## 9. Scalability Implications

### PostgreSQL Scalability

PostgreSQL is designed to **scale up** with the hardware:

- **Vertical scaling**: Increasing RAM (larger shared_buffers), CPUs (more parallel query workers), and disk IOPS directly improves performance.
- **Horizontal scaling**: Streaming replication allows read replicas. Logical replication enables more complex topologies. Citus extension provides sharding.
- **Partitioning**: Declarative table partitioning distributes data across multiple physical files.
- **Limits**: A single PostgreSQL instance can handle billions of rows, terabytes of data, and thousands of concurrent connections (via PgBouncer pooling).

Bottlenecks:
- VACUUM overhead on high-write tables.
- Connection overhead without pooling.
- No native sharding in core (requires extensions or application-level sharding).

### SQLite Scalability

SQLite is designed to **scale down**, not up:

- **Single writer bottleneck**: Only one write at a time, even in WAL mode. A write-heavy multi-user workload will serialize and degrade.
- **No network access**: Every consumer must have direct file access — unsuitable for distributed systems.
- **Practical limits**: SQLite officially supports databases up to 281 TB, but performance degrades well before that for large concurrent workloads.
- **Read scalability**: For read-heavy workloads on a single node, SQLite in WAL mode with multiple readers can be surprisingly performant.

SQLite's scalability strategy is different: rather than scaling one database to many users, you deploy **many SQLite databases** — one per user, per device, or per tenant (the "edge database" pattern used by Cloudflare D1, Turso, etc.).

---

## 10. Real-World Use Cases

### When to Use PostgreSQL

| Use Case | Reason |
|---|---|
| Web applications with multiple concurrent users | MVCC handles concurrent reads/writes gracefully |
| Financial systems | Full ACID, serializable isolation, strong durability |
| Analytics workloads | Parallel query, advanced indexing (GIN, BRIN), partitioning |
| Microservices needing a shared data store | Client-server model allows multiple services to connect |
| Applications requiring advanced types | JSONB, arrays, ranges, custom types, full-text search |
| Systems needing replication/HA | Streaming replication, logical replication, failover |

### When to Use SQLite

| Use Case | Reason |
|---|---|
| Mobile applications (iOS, Android) | Embedded, zero-config, single file, works offline |
| Desktop applications | Same reasons as mobile; no server setup required |
| Browsers and Electron apps | Firefox, Chrome, and many apps use SQLite internally |
| Testing and development | Lightweight, fast setup, in-memory mode available |
| IoT and edge devices | Minimal RAM/CPU, no network required |
| Single-user tools and CLIs | No concurrency needed; simplicity wins |
| Configuration/state storage | Replace INI/JSON files with a queryable format |

---

## 11. Answering the Suggested Questions

### Why does SQLite work well for mobile applications?

Mobile apps have requirements that directly match SQLite's design:
1. **No server setup**: App stores don't let you install a PostgreSQL server on a user's phone.
2. **Single user**: A mobile app typically has one user at a time — the concurrency limitations of SQLite are irrelevant.
3. **Offline-first**: SQLite is purely local — queries work with no network connection.
4. **Small footprint**: SQLite's library is ~600 KB. PostgreSQL's server binary is tens of MB.
5. **Atomic file operations**: Backing up user data = copying one `.db` file.

iOS and Android both include SQLite in their standard libraries, meaning every app on every phone already has SQLite available.

### Why is PostgreSQL preferred for large multi-user systems?

Three architectural reasons dominate:

1. **MVCC enables true concurrency**: In a web app with 500 concurrent users, you need reads and writes to happen simultaneously without blocking. PostgreSQL's tuple-level MVCC handles this elegantly. SQLite's single-writer model would serialize all writes, creating a throughput bottleneck.

2. **Client-server enables shared access**: PostgreSQL's network interface allows application servers, background workers, analytics tools, and admin tools to all connect independently and concurrently. SQLite requires direct file access — you can't have an app server in AWS and a DB file on a different machine.

3. **Row-level locking over file-level locking**: If two users update different rows in PostgreSQL, they proceed in parallel. In SQLite, one waits for the other's exclusive lock on the entire file.

### What architectural decisions lead to these differences?

The fundamental divergence is the **target deployment environment**:

- SQLite was designed by D. Richard Hipp as a replacement for flat files in an application — the "database as a file format." Every decision (single file, no server, in-process) flows from this goal.
- PostgreSQL was designed as a full relational DBMS for shared server use, descended from the POSTGRES research project at UC Berkeley. Every decision (client-server, MVCC, multi-process) flows from the need to handle concurrent, isolated, network-connected clients.

The concurrency model is the single most important difference: **MVCC with row-level locking** (PostgreSQL) vs **file-level locking with one writer** (SQLite). This architectural choice forces everything else — PostgreSQL needs a server process to manage locks and snapshots globally; SQLite can be a library because lock management is simple enough to handle in-process via OS file locks.

---

## Summary Table

| Dimension | PostgreSQL | SQLite |
|---|---|---|
| Architecture | Client-server | Embedded library |
| Process model | Multi-process (fork per connection) | Single application process |
| Storage engine | Heap files + separate indexes | Unified B-tree file |
| File organization | Directory with many files | Single `.db` file |
| Page size | 8 KB (default) | 4 KB (default, configurable) |
| Index types | B-Tree, Hash, GIN, GiST, BRIN | B-Tree only |
| Concurrency | MVCC + row-level locks | File-level locks (WAL improves reads) |
| Isolation levels | RC, RR, Serializable | Serializable only |
| Durability | WAL (redo log) | Rollback journal or WAL |
| Scalability | Vertical + horizontal (replicas) | Single node, single writer |
| Best for | Multi-user, enterprise, web apps | Embedded, mobile, single-user |

---

## References

- PostgreSQL Official Documentation — https://www.postgresql.org/docs/current/
- SQLite Architecture Overview — https://www.sqlite.org/arch.html
- SQLite File Format Specification — https://www.sqlite.org/fileformat.html
- PostgreSQL WAL Internals — https://www.postgresql.org/docs/current/wal-intro.html
- SQLite WAL Mode — https://www.sqlite.org/wal.html
- "SQLite Is Not a Toy Database" — Antonz blog
- The Design of Postgres (Stonebraker & Rowe, 1986)
