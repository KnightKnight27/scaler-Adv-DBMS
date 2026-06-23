# PostgreSQL vs SQLite — Architecture Comparison

**Student:** Romit Raj Sahu | 24BCS10436

---

## 1. Problem Background

SQLite and PostgreSQL solve fundamentally different problems, which is why their architectures look nothing alike.

SQLite was created in 2000 by D. Richard Hipp for the US Navy, originally to manage data on guided missile destroyers where installing a full database server was impractical. The constraint was clear: the database had to be a library, not a service. No installation, no configuration, no network. Just link it and go.

PostgreSQL traces back to the POSTGRES project at UC Berkeley (1986), itself a successor to Ingres. It was designed for multi-user, multi-application environments where many clients simultaneously read and write, correctness under concurrent load is mandatory, and the database must outlive any individual application.

The difference in origin explains every architectural decision that follows.

---

## 2. Architecture Overview

### SQLite: In-Process Library

```
Application Process
┌──────────────────────────────────────────┐
│                                          │
│   Application Code                       │
│        │                                 │
│        ▼                                 │
│   SQLite Library (linked in)             │
│   ┌──────────────────────────────────┐   │
│   │  SQL Compiler                    │   │
│   │  VDBE (Virtual Machine)          │   │
│   │  B-Tree Engine                   │   │
│   │  Pager (Page Cache)              │   │
│   │  OS Interface (VFS)              │   │
│   └──────────────────────────────────┘   │
│        │                                 │
└────────┼─────────────────────────────────┘
         │
         ▼
   Single .db file on disk
```

SQLite is not a server. It is a C library that the application links against. There is no separate process, no socket, no authentication handshake. The application directly calls SQLite functions, which read and write a single file.

### PostgreSQL: Multi-Process Server

```
Client Applications
┌──────────┐  ┌──────────┐  ┌──────────┐
│  psql    │  │  App 1   │  │  App 2   │
└────┬─────┘  └────┬─────┘  └────┬─────┘
     │              │              │
     └──────────────┴──────────────┘
                    │ TCP / Unix Socket
                    ▼
           Postmaster (Listener)
           ┌─────────────────┐
           │  Accept conn    │
           │  fork() backend │
           └────────┬────────┘
                    │ forks
        ┌───────────┼───────────┐
        ▼           ▼           ▼
   Backend 1   Backend 2   Background Workers
   (per conn)  (per conn)  ┌─────────────────┐
                           │ bgwriter        │
                           │ checkpointer    │
                           │ autovacuum      │
                           │ WAL writer      │
                           └─────────────────┘
                    │
                    ▼
           Shared Memory
           ┌─────────────────────────┐
           │ shared_buffers (8KB pp) │
           │ WAL buffers             │
           │ Lock table              │
           │ Proc array              │
           └─────────────────────────┘
                    │
                    ▼
           Data Directory (many files)
```

PostgreSQL is a server. The postmaster listens for connections and forks a dedicated backend process for each one. All backends share a common memory segment (shared_buffers) and coordinate through lock tables and the process array stored there.

---

## 3. Internal Design

### 3.1 Storage

**SQLite — single file, everything inside:**

The entire database lives in one file. The file is divided into fixed-size pages (default 4096 bytes). There are three kinds of pages:

- **B-tree interior page** (`0x05`): holds separator keys and child page pointers
- **B-tree leaf page** (`0x0D`): holds actual row data (for table pages) or key+rowid (for index pages)
- **Overflow page**: for rows that don't fit in one page

Table pages are leaf pages of the table's B-tree. SQLite stores the whole row inline in the B-tree leaf. This is essentially a clustered index design — every table has an implicit rowid (or an explicit INTEGER PRIMARY KEY which becomes the rowid), and rows are stored in rowid order.

```
SQLite file layout:
┌─────────────────────────────────────────┐
│ File header (100 bytes)                 │
│ Page 1: schema table (sqlite_schema)    │
│ Page 2..N: table/index B-tree pages     │
│            + overflow pages             │
│            + freelist pages             │
└─────────────────────────────────────────┘

Single page (4096 bytes):
┌──────────────────────────────────────┐
│ Page header (8 or 12 bytes)          │
│ Cell pointer array (2 bytes/cell)    │
│                      ↕ free space    │
│ Cell data (rows, grows downward)     │
└──────────────────────────────────────┘
```

**PostgreSQL — directory of heap files:**

Each table is a separate file (or a set of 1GB segments if large). The file is a sequence of 8KB pages. These are heap pages — rows are not stored in any sorted order.

```
PostgreSQL data/base/<db_oid>/<table_oid>
data/base/<db_oid>/<table_oid>_fsm     (free space map)
data/base/<db_oid>/<table_oid>_vm      (visibility map)
data/base/<db_oid>/<index_oid>         (index file, separate)

Heap page (8192 bytes):
┌────────────────────────────────────────────┐
│ PageHeader (24 bytes)                      │
│   - lsn: last WAL record for this page     │
│   - pd_lower: end of ItemId array          │
│   - pd_upper: start of tuple data          │
│   - pd_special: start of special space     │
├────────────────────────────────────────────┤
│ ItemId array [item1][item2]...[itemN]       │
│ (4 bytes each: offset + length + flags)    │
│                                            │
│              free space                    │
│                                            │
│ HeapTuple N ... HeapTuple 2  HeapTuple 1   │
│ (tuples grow downward from pd_upper)       │
└────────────────────────────────────────────┘

HeapTuple header includes:
  xmin: transaction that inserted this tuple
  xmax: transaction that deleted this tuple (0 if live)
  ctid: pointer to newer version of this tuple
```

### 3.2 Transaction Management and Concurrency

This is where the difference is most significant.

**SQLite WAL mode (default since 3.7.0):**

SQLite has two journal modes. In WAL (Write-Ahead Log) mode:
- Writers append changes to a separate WAL file instead of modifying the main database file directly
- Readers read from the main file and check the WAL for newer versions of pages
- Only ONE writer can be active at a time — writes are serialized
- Multiple concurrent readers are possible even while a writer is active

The one-writer constraint is not a bug. For the use cases SQLite targets (mobile apps, embedded systems, single-user desktop tools), concurrent writes are rare. The simplicity of having no lock manager or transaction coordinator is a deliberate trade for those environments.

**PostgreSQL MVCC:**

PostgreSQL maintains multiple versions of every row. When a row is updated, the old version is not deleted immediately — its `xmax` is stamped with the updating transaction's ID and a new version is inserted with `xmin` set. A reader checks whether a tuple version is visible to its snapshot by evaluating:

```
Tuple is visible if:
  xmin committed AND xmin < snapshot.xmin
  AND (xmax is invalid
       OR xmax not committed
       OR xmax > snapshot.xmax
       OR xmax is in snapshot's in-progress list)
```

This allows fully concurrent reads and writes with no read locks. A reader and a writer on the same row never block each other. The cost: old dead tuple versions accumulate in the heap and must be periodically reclaimed by VACUUM.

### 3.3 Index Implementation

Both use B-trees, but with different details:

**SQLite:** Table rows ARE the B-tree leaves. The rowid is the key. An index on a column is a separate B-tree whose leaves store (indexed column value, rowid) pairs. A query using an index does: find rowid in index B-tree → use rowid to look up the row in the table B-tree.

**PostgreSQL:** The table heap and indexes are separate. An index leaf stores (key value, TID) where TID = (page number, slot number within page). Looking up a row by index: find TID in index → fetch the exact page and slot from the heap.

---

## 4. Design Trade-offs

| Dimension | SQLite | PostgreSQL |
|-----------|--------|------------|
| Deployment | Zero — link a library | Install a server process |
| Write concurrency | Serialized (one writer) | Fully concurrent via MVCC |
| Read concurrency | Many concurrent readers | Many concurrent readers |
| File organization | Single file | Directory of files |
| Row storage | Clustered (rowid B-tree) | Heap (unsorted) |
| Max concurrent writes | 1 | Limited only by hardware |
| VACUUM needed? | No (overwrite old data) | Yes (dead tuple cleanup) |
| Network access | No — must be on same host | Yes — client-server protocol |
| Suitable database size | Up to a few GB practical | Tens of terabytes |
| Crash safety | WAL protects against crash | WAL + background checkpointing |

**Why SQLite suits mobile applications:**
The app IS the only writer. There is no other process. Serialized writes are not a limitation because there is only one process writing. A single file is ideal — it can be backed up with `cp`, emailed, inspected with standard tools. Zero configuration matters when the developer cannot control the deployment environment. The Android, iOS, and browser environments all embed SQLite for exactly these reasons.

**Why PostgreSQL is preferred for web applications:**
A web server handling 500 requests per second may have dozens of concurrent database connections all trying to write. SQLite's single-writer model would serialize every write, creating a bottleneck. PostgreSQL's MVCC allows concurrent writers on different rows without blocking. The client-server model also means the database runs on a dedicated machine, isolated from application memory, sharable across multiple application servers.

**The hidden cost of MVCC:**
PostgreSQL's MVCC is powerful but creates a maintenance burden. Dead tuple versions accumulate in the heap. Without VACUUM, heap pages fill up with dead rows, queries slow down, index bloat grows, and eventually transaction ID wraparound threatens data integrity. SQLite simply overwrites rows in place — when a row is updated, the old version is gone. There is no cleanup burden.

---

## 5. Experiments and Observations

### Observation 1: SQLite page type bytes in hex dump

A hex dump of a SQLite database file with an interior B-tree node shows the pattern:

```
Offset 0 (file header): 53 51 4C 69 74 65 20 66 6F 72 6D 61 74 20 33 00
                         S  Q  L  i  t  e     f  o  r  m  a  t     3

Page 1 byte 0:  0D  → leaf B-tree page
Page 2 byte 0:  05  → interior B-tree page (only appears when table is large enough)
```

This directly shows that SQLite uses a single integer to encode node type, and the entire file structure is self-describing.

### Observation 2: PostgreSQL process model via ps aux

On a freshly started PostgreSQL instance with no connections:

```bash
$ ps aux | grep postgres
postgres  1234  postmaster -D /var/lib/pgsql/data
postgres  1235  postgres: checkpointer
postgres  1236  postgres: background writer
postgres  1237  postgres: WAL writer
postgres  1238  postgres: autovacuum launcher
postgres  1239  postgres: stats collector
```

Six processes before a single client connects. SQLite has zero background processes — it is purely reactive.

### Observation 3: SQLite mmap vs no-mmap query timing

On a Northwind database with a three-table JOIN and GROUP BY:
- Without mmap: ~230ms
- With mmap (PRAGMA mmap_size = 268435456): ~140ms

The speedup comes from letting the OS page cache serve reads directly without copying through SQLite's internal pager. The trade-off: the application loses control over which pages are in memory, which is exactly why production databases prefer buffer pool management to mmap.

---

## 6. Key Learnings

**The fundamental insight:** SQLite and PostgreSQL do not compete. They are optimized for opposite assumptions. SQLite assumes one process, one database, low concurrency, maximum simplicity. PostgreSQL assumes many processes, shared database, high concurrency, maximum correctness.

**Embedded vs client-server is not about capability:** SQLite can handle full SQL, foreign keys, triggers, JSON, and most of what PostgreSQL offers at the query language level. The architecture difference is about the process model and concurrency, not SQL features.

**MVCC is not free:** PostgreSQL's ability to serve concurrent readers and writers without blocking each other is architecturally elegant but creates the VACUUM maintenance problem. Every trade-off in database design has this character — the benefit is visible immediately, the cost accumulates over time.

**Single file is underrated:** The ability to copy, move, email, or `rsync` an entire database as a single file is a genuine operational advantage for many use cases. PostgreSQL's multi-file design enables features (tablespaces, TOAST, per-table vacuuming) that are irrelevant for most embedded uses.
