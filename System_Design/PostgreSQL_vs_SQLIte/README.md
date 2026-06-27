# System Design Analysis: PostgreSQL vs. SQLite3

**Name:** Pratham Onkar Singh

**Roll No:** 24bcs10136


When developers talk about databases, PostgreSQL and SQLite are often thrown into the same conversation simply because both speak SQL. Under the hood, however, they are built on completely opposing architectural philosophies.

PostgreSQL is a heavy, multi-process network server built for high concurrency and strict data isolation. SQLite is a tiny, serverless C library designed to live directly inside your application's memory space. This write-up explores the engineering trade-offs behind these two engines and examines how their internal mechanics dictate their real-world performance.

---

# 1. Problem Background

To understand why an architecture looks the way it does, you have to look at the historical friction it was trying to eliminate.

## SQLite: Killing Configuration Hell

In the late 1990s and early 2000s, local desktop and embedded software faced an annoying problem. If an app needed to save structured data, developers had two bad options:

- Write custom parsers for raw `.ini` or `.xml` files.
- Force users to install and maintain a heavyweight database server like MySQL or Oracle.

SQLite was created to eliminate this problem.

Its primary design goal wasn't to replace enterprise database servers but to replace custom disk-file storage. The database became a single portable file while exposing a transactional SQL engine compiled directly into the application.

## PostgreSQL: Concurrency Without Data Corruption

PostgreSQL evolved from the UC Berkeley **POSTGRES** project in 1986.

Enterprise systems were rapidly moving toward multi-user client-server architectures, and existing databases struggled with:

- Write contention
- Rigid type systems

PostgreSQL was designed around:

- Strict ACID guarantees
- Massive concurrent workloads
- Deep extensibility through custom data types, index methods, and procedural languages

---

# 2. Architecture Overview

The defining architectural difference between these two systems is **process boundary isolation**.

## SQLite3: In-Process Embedded Model

```text
+-------------------------------------------------------+
| SQLite3: In-Process Embedded Model                    |
|                                                       |
|  Host Application Process                             |
|  ├── Your Application Logic                           |
|  └── libsqlite3.so (Direct C Function Calls)          |
|             │                                         |
|             ▼                                         |
|      OS Filesystem Calls                              |
|      (open / read / write)                            |
|             │                                         |
|             ▼                                         |
|      app_storage.db                                   |
|      (Single Database File)                           |
+-------------------------------------------------------+
```

## PostgreSQL: Multi-Process Client-Server Model

```text
+-------------------------------------------------------+
| PostgreSQL: Multi-Process Client-Server Model         |
|                                                       |
| Client Application                                    |
|          │                                            |
|          ▼                                            |
|     TCP / Unix Socket                                 |
|          │                                            |
|          ▼                                            |
|      Postmaster                                       |
|          │                                            |
|        forks                                          |
|          ▼                                            |
|     Backend Worker Process                            |
|          │                                            |
|   ┌──────┴─────────┐                                  |
|   ▼                ▼                                  |
| Shared Buffers   WAL Writer                           |
|   │                │                                  |
|   ▼                ▼                                  |
| Heap Files      pg_wal/                               |
+-------------------------------------------------------+
```

### SQLite's Execution Path

SQLite has **no database server process**.

It is simply a library linked directly into the application.

Executing a SQL statement becomes a normal C function call:

- No network latency
- No sockets
- No inter-process communication (IPC)
- No serialization overhead

### PostgreSQL's Execution Path

PostgreSQL uses a **process-per-connection** architecture.

A master daemon (`postmaster`) listens on port **5432**.

Each new client connection causes PostgreSQL to:

1. Fork a backend process.
2. Allocate shared memory access.
3. Coordinate with background workers such as the WAL Writer.

---

# 3. Internal Design

## 3.1 Storage & File Organization

### SQLite (Clustered B+Tree)

- Entire database stored inside a single file.
- Default page size: **4 KB**
- Tables stored as **Clustered B+Trees** keyed by `rowid`.
- Leaf nodes contain the actual row data.

### PostgreSQL (Directory of Heap Files)

- Every table and index has its own file.
- Large files are split into **1 GB segments**.
- Uses **8 KB pages**.
- Tables stored as unordered **Heap Files**.
- Indexes point to physical **Tuple IDs (TIDs)** consisting of:
  - Block Number
  - Line Offset

---

## 3.2 Memory & Buffer Management

### SQLite

SQLite maintains a simple page cache.

Because it shares memory with the host application, it can leverage **memory-mapped I/O (`mmap`)**.

When enabled:

- Disk pages are mapped directly into virtual memory.
- Data is accessed using pointers.
- Fewer expensive `read()` system calls are required.

### PostgreSQL

PostgreSQL allocates a global cache called:

```text
shared_buffers
```

Instead of standard LRU replacement, PostgreSQL uses the **Clock Sweep** algorithm.

Each buffer maintains:

- Usage count (0–5)
- Pin count

The clock hand continuously scans buffers:

- Skip pinned pages.
- Decrement positive usage counts.
- Evict the first unpinned page with usage count zero.

This prevents large sequential scans from flushing frequently used transactional pages.

---

## 3.3 Indexing & Concurrency Control

### SQLite (File-Level Locking)

SQLite uses traditional B-Trees.

In rollback journal mode:

- Entire database file is locked for writes.

Even in WAL mode:

- Multiple readers are allowed.
- Only **one writer** may execute at a time.

---

### PostgreSQL (MVCC & Lehman-Yao B-Trees)

PostgreSQL implements **Multi-Version Concurrency Control (MVCC).**

When updating a row:

1. Old tuple receives an `xmax`.
2. New tuple is written elsewhere.

Readers determine visibility using transaction snapshots rather than blocking writers.

For indexes, PostgreSQL uses **Lehman & Yao B-Trees**.

These include horizontal **Right-Link pointers** between sibling pages, allowing concurrent page splits without locking upper tree branches.

---

# 4. Design Trade-Offs

| Engineering Dimension | SQLite3 | PostgreSQL |
|------------------------|----------|------------|
| **Write Scalability** | Severe bottleneck. Writes serialize at the database level. Excellent for single-writer workloads but poor under concurrent traffic. | High scalability. MVCC allows many concurrent writers without blocking readers. |
| **I/O Latency** | Extremely low. Direct function calls avoid networking entirely. | Moderate. Queries travel through sockets and kernel context switches. |
| **Maintenance Bloat** | Minimal. Simple page overwrites or file appends. | High. MVCC creates dead tuples requiring continuous `VACUUM`. |
| **Crash Blast Radius** | Higher. Since the database shares the application's process, severe application corruption can affect database state. | Lower. Client crashes do not directly impact the database engine processes. |

---

# 5. Experiments & Observations

To compare both systems, we created identical schemas on fresh local installations.

## SQLite3: Footprint Inspection

```sql
CREATE TABLE users (
    id INT,
    username TEXT
);

PRAGMA page_size;
PRAGMA page_count;
```

Results:

```text
page_size  = 4096
page_count = 2
```

### Takeaway

An empty SQLite database occupies exactly:

```text
4096 × 2 = 8192 bytes (8 KB)
```

Page allocation:

- Page 1 → Database header + schema
- Page 2 → Root B-Tree node for `users`

The storage overhead is extremely small.

---

## PostgreSQL: Catalog Planning Overhead

Execution plan:

```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT *
FROM users;
```

Output:

```text
Seq Scan on users
(actual time=0.006..0.007 rows=3 loops=1)

Buffers: shared hit=1

Planning:
  Buffers: shared hit=52

Planning Time: 0.210 ms
Execution Time: 0.024 ms
```

### Takeaway

Executing the query required only:

- **1 shared buffer hit**

However, planning required:

- **52 shared catalog buffer hits**

Before execution, PostgreSQL consulted system catalogs to verify:

- Permissions
- Data types
- Statistics

Enterprise flexibility introduces substantial planning overhead compared to SQLite.

---

# 6. Key Learnings & Suggested Questions Answered

## Why does SQLite work so well for mobile applications?

Mobile devices have strict limits on:

- RAM
- Storage
- Battery

SQLite is ideal because:

- Binary size is only around **500 KB**
- No background server
- No installation or configuration
- Entire database lives inside one `.db` file
- Easy synchronization with cloud backup systems

Additionally, mobile apps are almost always single-user applications, so SQLite's single-writer limitation rarely becomes a bottleneck.

---

## Why is PostgreSQL preferred for large multi-user systems?

Enterprise web applications routinely process many concurrent requests.

If 50 users submit orders simultaneously:

- SQLite serializes writes.
- PostgreSQL allows all transactions to proceed concurrently using MVCC.

Running the database in isolated operating system processes also protects database integrity even if the application server crashes.

---

## What architectural decisions lead to these differences?

The divergence comes down to two major design choices.

### Library vs. Daemon

**SQLite**

- Embedded library
- Direct function calls
- Extremely low latency
- Limited concurrency

**PostgreSQL**

- Dedicated database server
- Shared memory
- Background workers
- High concurrency
- Network overhead

---

### Clustered Storage vs. Heap Storage

**SQLite**

- Tables stored as clustered B+Trees
- Primary key lookups are extremely efficient

**PostgreSQL**

- Tables stored as heap files
- Separate indexes reference physical tuple locations
- Supports many advanced index types such as:

  - B-Tree
  - GIN
  - GiST
  - BRIN

This flexibility makes PostgreSQL much more suitable for complex enterprise workloads.