# Lab 2: SQLite3 Internals — mmap, Page Size, PRAGMA & Library Architecture

**Name:** Rachit S  
**Roll Number:** 24bcs10139  
**Course:** Advanced Database Management Systems (AdvDBMS)

---

## 1. Overview
This experiment compares the storage models, caching pathways, and resource utilization profiles of **SQLite3** (an in-process relational database library) and **PostgreSQL** (a client-server daemon database system). We load 200,000 generated user records into both databases, perform metadata introspection via low-level administration commands (PRAGMA / SHOW), and analyze table-scan performance with memory-mapping (`mmap_size`) enabled and disabled.

---

## 2. Environment Settings

- **SQLite3 Version:** 3.53.0
- **PostgreSQL Version:** 17.4
- **Logical Data Rows:** 200,000
- **Scan Target:** `SELECT * FROM users;` (Redirection to `NUL` to measure database throughput without console output bottlenecks).

---

## 3. SQLite3 Experiments & PRAGMAs

### Database Generation
We initialize the database `users_sqlite.db` with a recursive sequence:

```sql
PRAGMA journal_mode=OFF;
PRAGMA synchronous=OFF;

CREATE TABLE users (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  email TEXT NOT NULL,
  created_at TEXT NOT NULL
);

WITH RECURSIVE seq(n) AS (
  SELECT 1
  UNION ALL
  SELECT n + 1 FROM seq WHERE n < 200000
)
INSERT INTO users (name, email, created_at)
SELECT 'User ' || n,
       'user' || n || '@example.com',
       datetime('now', '-' || (n % 365) || ' days')
FROM seq;

ANALYZE;
```

### Introspection Commands & Results

```sql
PRAGMA page_size;     -- Returns: 4096 (4 KB default)
PRAGMA page_count;    -- Returns: 3058
```
- **Total SQLite File Size:** 11.95 MB (Calculated as `page_size * page_count = 12,525,568` bytes).
- **Default Memory Map Size:** `PRAGMA mmap_size;` returned `0` bytes.

We enable memory-mapped reads:
```sql
PRAGMA mmap_size = 268435456; -- Set to 256 MB
```

### Scan Performance with and without Memory-Mapping

| Scan Run | Time (mmap disabled) | Time (mmap enabled - 256MB) |
| :--- | :--- | :--- |
| Run 1 | 0.591 s | 0.548 s |
| Run 2 | 0.582 s | 0.535 s |
| Run 3 | 0.554 s | 0.528 s |
| Run 4 | 0.563 s | 0.542 s |
| Run 5 | 0.567 s | 0.590 s |
| **Average** | **0.571 s** | **0.549 s** |

---

## 4. PostgreSQL Experiments & Introspection

### Database Setup
We create the table and seed 200,000 records:

```sql
CREATE TABLE users (
  id integer PRIMARY KEY,
  name text NOT NULL,
  email text NOT NULL,
  created_at timestamp NOT NULL
);

INSERT INTO users (id, name, email, created_at)
SELECT n,
       'User ' || n,
       'user' || n || '@example.com',
       now() - ((n % 365) || ' days')::interval
FROM generate_series(1, 200000) AS n;

ANALYZE users;
```

### Introspection Commands & Results

```sql
SHOW block_size;       -- Returns: 8192 (8 KB default block size)
SHOW shared_buffers;  -- Returns: 128MB
```

We query the relation page details:
```sql
SELECT pg_size_pretty(pg_relation_size('users')) AS table_size,
       pg_relation_size('users') / current_setting('block_size')::int AS page_count;
```
- **PostgreSQL Table File Size:** 15 MB
- **PostgreSQL Block Count:** 1,870 pages (at 8 KB per block)

### Scan Performance
Scanning the table (`SELECT * FROM users;` to NUL) over 5 runs:
- Average Time: **0.463 seconds**

---

## 5. Architectural Differences: Library vs Server

```
   [SQLite3 Architecture]                     [PostgreSQL Architecture]
  
  +----------------------+                  +--------------------------+
  | Application Binary   |                  | Application / CLI Client |
  | (links libsqlite3)   |                  +--------------------------+
  +----------------------+                               | (TCP/Unix Socket)
             | (Direct OS System Calls)                  v
  +----------------------+                  +--------------------------+
  | database_file.db     |                  | PostgreSQL Server Daemon |
  +----------------------+                  | (shared_buffers, catalog)|
                                            +--------------------------+
                                                         | (Disk I/O)
                                            +--------------------------+
                                            | Data Files Directory     |
                                            +--------------------------+
```

### Key Differences Matrix

| Feature | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Process Model** | Embedded library running in application's memory space. | Client-server architecture running as a background system daemon. |
| **I/O Strategy** | Direct filesystem access (optional memory-mapping). | Custom shared buffer page manager (`shared_buffers`) with double buffering. |
| **Concurrency** | Database-level database locks (single-writer locks). | Multi-Version Concurrency Control (MVCC) with fine-grained row-level locking. |
| **Security** | Leverages system filesystem permissions. | Comprehensive role, user, permission schemas, and SSL encryption. |

---

## 6. Key Takeaways
- **SQLite3** is highly optimized for localized, serverless, single-client environments. Its simplicity provides low overhead, and enabling `mmap_size` further boosts read performance by bypassing traditional OS `read()` copy cycles.
- **PostgreSQL** trades file size and initialization latency for scale. Its dedicated buffer cache manager and daemon architecture allow it to orchestrate massive concurrent read-write workloads efficiently, outperforming SQLite in query scans on larger datasets due to better page density (8 KB blocks vs 4 KB pages) and multi-threaded scheduling.
