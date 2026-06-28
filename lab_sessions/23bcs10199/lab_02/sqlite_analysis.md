# Lab 2 — SQLite3 Internals + DB Comparison Report

**Student:** Indrajeet Yadav | **Roll No:** 23BCS10199

---

## Part 1: SQLite3 Installation & Verification

```bash
# Arch / CachyOS
sudo pacman -S sqlite

# Ubuntu / Debian
sudo apt install sqlite3 libsqlite3-dev

# Verify
sqlite3 --version
# e.g.: 3.45.1 2024-01-30 16:01:20 e876e51a0ed5c5b3126f52e532044363a014bc594cfefa787

# Create a test database
sqlite3 students.db
```

---

## Part 2: Storage Internals via PRAGMA

Open the database and run these introspection queries one by one:

```sql
-- Page size (bytes per page; set at creation, cannot change after)
PRAGMA page_size;
-- Result: 4096    (matches Linux PAGE_SIZE — no sub-page I/O needed)

-- Total number of pages currently allocated
PRAGMA page_count;
-- Result: 2       (page 1 = file header + first B-tree root; page 2 = first data page)

-- Total file size = page_size * page_count
-- 4096 * 2 = 8192 bytes

-- WAL (Write-Ahead Log) mode — far better concurrent read performance
PRAGMA journal_mode = WAL;
PRAGMA journal_mode;
-- Result: wal

-- Number of pages held in the in-process page cache
PRAGMA cache_size;
-- Result: -2000   (negative = KiB; 2000 KiB = ~488 pages of 4096 bytes each)

PRAGMA cache_size = -8000;  -- increase to 8 MB in-process cache
```

### What the page layout looks like

```
sqlite3_students.db (on disk)
┌──────────────────────┐  ← byte 0
│  Page 1 (4096 bytes) │
│  ┌────────────────┐  │
│  │ File Header    │  │  ← bytes 0-99: magic string "SQLite format 3\0",
│  │ (100 bytes)    │  │    page_size, file_change_counter, schema version, etc.
│  ├────────────────┤  │
│  │ B-tree root    │  │  ← sqlite_schema table root node
│  │ (interior or   │  │
│  │  leaf node)    │  │
│  └────────────────┘  │
├──────────────────────┤  ← byte 4096
│  Page 2 (4096 bytes) │
│  B-tree node for     │
│  first table         │
└──────────────────────┘
```

---

## Part 3: mmap — Memory-Mapped I/O

### Without mmap (default)

```bash
strace -e trace=read,pread64,mmap sqlite3 students.db "SELECT count(*) FROM sqlite_schema;" 2>&1 | grep -E "pread|read"
# You'll see many pread64() calls — one per page needed
```

### Enable mmap

```sql
-- Enable 256 MB of mmap — maps the first 256 MB of the file into process address space
PRAGMA mmap_size = 268435456;
PRAGMA mmap_size;
-- Result: 268435456
```

### With mmap enabled

```bash
strace -e trace=mmap,pread64 sqlite3 students.db "SELECT count(*) FROM sqlite_schema;" 2>&1
# You'll see: mmap(NULL, 8192, PROT_READ, MAP_SHARED, 3, 0) = 0x7f...
# Followed by far fewer (or zero) pread64() calls — the OS serves reads from the mapped region
```

### Why mmap is faster for reads

```
Without mmap:
  Application calls read() → kernel copies page from page cache → user buffer
  (two copies: disk → kernel cache → user buffer)

With mmap:
  mmap() maps file pages into process address space
  Application accesses memory directly → page fault on first access → OS maps page
  (one copy: disk → kernel page cache, shared with process via virtual memory)
  Subsequent accesses: zero syscalls, zero copies — just a memory read
```

---

## Part 4: SQLite is a Library, Not a Server

### Architecture comparison

```
Your Application (e.g., a C++ binary)            vs.    psql or any PG client
───────────────────────────────────────────────         ─────────────────────
┌──────────────────────────────────┐                    ┌──────────────┐
│  Your application code           │                    │  psql client │
│  + libsqlite3.so (linked in)     │                    └──────┬───────┘
│    │                             │                           │  TCP/Unix socket
│    ├── sqlite3_open()            │                           │  (port 5432)
│    ├── sqlite3_exec()            │                           ▼
│    └── sqlite3_close()           │                    ┌──────────────────┐
│                                  │                    │  postgres daemon  │
│  reads/writes .db file directly  │                    │  (separate proc)  │
│  via OS syscalls (pread/pwrite)  │                    │  + WAL writer     │
└──────────────────────────────────┘                    │  + autovacuum     │
                                                        │  + background     │
                                                        │    workers        │
                                                        └──────────────────┘
```

Verify no SQLite server is running:
```bash
ps aux | grep sqlite
# Nothing — only your own process. SQLite HAS no daemon.

ldd $(which sqlite3)
# libsqlite3.so.0 => /usr/lib/libsqlite3.so.0 (0x00007f...)
# The sqlite3 CLI is just a thin wrapper around the library.
```

---

## Part 5: PRAGMA Deep Dive

```sql
-- Open the database
sqlite3 students.db

-- Create a sample table
CREATE TABLE IF NOT EXISTS students (
    id       INTEGER PRIMARY KEY,
    name     TEXT    NOT NULL,
    roll     TEXT    UNIQUE NOT NULL,
    gpa      REAL    DEFAULT 0.0
);

INSERT INTO students VALUES
    (1, 'Indrajeet Yadav',  '23BCS10199', 9.1),
    (2, 'Alice Sharma',     '23BCS10200', 8.7),
    (3, 'Bob Verma',        '23BCS10201', 7.5),
    (4, 'Carol Singh',      '23BCS10202', 9.4),
    (5, 'Dave Kumar',       '23BCS10203', 6.8);

-- Inspect storage after inserts
PRAGMA page_count;
-- pages increase as data fills the B-tree

-- Inspect table structure
PRAGMA table_info(students);
-- cid | name | type    | notnull | dflt_value | pk
--  0  | id   | INTEGER |    0    |            |  1
--  1  | name | TEXT    |    1    |            |  0
-- ...

-- List all indexes on the students table
PRAGMA index_list(students);

-- Show the B-tree structure SQLite uses internally
PRAGMA integrity_check;
-- Result: ok

-- WAL checkpoint: flush WAL file back into main db
PRAGMA wal_checkpoint(FULL);

-- List all attached databases
PRAGMA database_list;
-- seq | name | file
--  0  | main | /path/to/students.db

-- Shrink the database file (reclaims freed pages)
VACUUM;
PRAGMA page_count;  -- may decrease
```

---

# System Design Assignment 1: DB Comparison — PostgreSQL vs SQLite vs DuckDB

## 1. The Three Engines

| Engine | Design Point | Storage Layout | Concurrency |
|--------|-------------|----------------|-------------|
| **PostgreSQL 18** | Multi-tenant transactional server | Row store (heap + B-tree indexes) | Process-per-connection, MVCC, parallel queries |
| **SQLite 3.45** | Single-process embedded library | Row store (B-tree pages in one file) | Many readers + one writer (WAL mode), no parallelism |
| **DuckDB 1.1** | Single-process embedded analytics | Column store (compressed blocks + zone maps) | Vectorized, multi-threaded scans, MVCC |

---

## 2. Schema and Data

```sql
CREATE TABLE users (
    user_id     BIGINT PRIMARY KEY,
    country     TEXT NOT NULL,        -- 20 countries, power-law distribution
    signup_date DATE NOT NULL,
    is_premium  BOOLEAN NOT NULL      -- ~10%, biased toward older accounts
);

CREATE TABLE orders (
    order_id   BIGINT PRIMARY KEY,
    user_id    BIGINT NOT NULL REFERENCES users(user_id),
    order_date DATE NOT NULL,
    amount     NUMERIC(10,2) NOT NULL,
    status     TEXT NOT NULL          -- 'completed' 78%, 'pending' 15%, 'cancelled' 7%
);

-- Indexes (PostgreSQL and SQLite — DuckDB scans columns directly)
CREATE INDEX idx_orders_user_id    ON orders (user_id);
CREATE INDEX idx_orders_order_date ON orders (order_date);
CREATE INDEX idx_orders_status     ON orders (status);
CREATE INDEX idx_users_country     ON users  (country);
```

Volume: `users` = **100,000 rows**, `orders` = **1,000,000 rows**

---

## 3. Methodology

- Load CSVs once, build indexes after bulk load, run `ANALYZE` / `PRAGMA optimize` so each planner has fresh statistics.
- Per query: 1 warm-up run discarded, then 5 measured runs. Report the mean.
- Caches warm — cold-cache numbers mostly measure SSD latency, not DB engine quality.
- Single persistent connection reused across runs to exclude connection overhead.

---

## 4. The Five Queries

| # | Query | What it stresses |
|---|-------|-----------------|
| **Q1** | Top 10 countries by revenue from completed orders | Big hash join + agg + sort/limit |
| **Q2** | Premium users' spend in last 90 days, ≥ 3 orders | Selective filter + join + group |
| **Q3** | All orders for `user_id = 42` | Indexed point lookup (OLTP shape) |
| **Q4** | Monthly revenue trend, completed orders | Full scan + date-truncated group |
| **Q5** | First order per user + days-to-next-order | Window functions over 100k partitions |

```sql
-- Q1: Top-10 countries by revenue from completed orders
SELECT u.country, SUM(o.amount) AS revenue
FROM orders o JOIN users u ON o.user_id = u.user_id
WHERE o.status = 'completed'
GROUP BY u.country
ORDER BY revenue DESC
LIMIT 10;

-- Q2: Premium users' spend in last 90 days with ≥ 3 orders
SELECT u.user_id, u.country, COUNT(*) AS order_count, SUM(o.amount) AS total
FROM users u JOIN orders o ON u.user_id = o.user_id
WHERE u.is_premium = TRUE
  AND o.order_date >= CURRENT_DATE - INTERVAL '90 days'
  AND o.status = 'completed'
GROUP BY u.user_id, u.country
HAVING COUNT(*) >= 3
ORDER BY total DESC;

-- Q3: Point lookup — all orders for one user
SELECT * FROM orders WHERE user_id = 42 ORDER BY order_date;

-- Q4: Monthly revenue trend
SELECT DATE_TRUNC('month', order_date) AS month,
       SUM(amount) AS revenue,
       COUNT(*) AS orders
FROM orders
WHERE status = 'completed'
GROUP BY 1
ORDER BY 1;

-- Q5: First order + days-to-next per user (window function)
SELECT user_id, order_id, order_date, amount,
       FIRST_VALUE(order_date) OVER w AS first_order_date,
       LEAD(order_date) OVER w - order_date AS days_to_next
FROM orders
WINDOW w AS (PARTITION BY user_id ORDER BY order_date);
```

---

## 5. Storage Footprint *(illustrative)*

| | PostgreSQL | SQLite | DuckDB |
|--|-----------|--------|--------|
| Whole DB on disk | ≈ 120 MB | ≈ 90 MB | ≈ 48 MB |
| `orders` table + indexes | ≈ 62 MB heap + 35 MB indexes | ≈ 78 MB B-tree | ≈ 40 MB columnar+compressed |
| Page / block size | 8 KiB | 4 KiB | 256 KiB |
| Compression | TOAST (long values only) | None | LZ4 / FSST per column block |
| Files on disk | Many under `$PGDATA` | One `.db` file | One `.db` file |

DuckDB compresses each column independently, so `status` (3 distinct values) and `country` (20 distinct values) collapse dramatically. PostgreSQL pays overhead for per-row MVCC visibility metadata (xmin, xmax, ctid) stored in every heap tuple.

---

## 6. Per-Query Timings *(illustrative — 5 warm runs, mean wall time)*

| Query | PostgreSQL | SQLite | DuckDB | Winner |
|-------|----------:|-------:|-------:|--------|
| Q1 Top-10 countries | **55 ms** | 210 ms | 65 ms | PG (parallel hash agg) |
| Q2 Premium 90-day | **85 ms** | 185 ms | 95 ms | PG (selective + index) |
| Q3 Point lookup user=42 | 0.3 ms | **0.2 ms** | 240 ms | SQLite (B-tree dive, no startup) |
| Q4 Monthly revenue | 95 ms | 230 ms | **45 ms** | DuckDB (columnar win) |
| Q5 Window per user | 320 ms | 1,100 ms | **180 ms** | DuckDB (vectorized partition) |

Three different engines win three different categories — the point of the comparison.

---

## 7. Why the Timings Look That Way

### Q1 / Q2 — PostgreSQL wins

PostgreSQL runs a **parallel hash aggregate** (2 workers by default). Both workers partial-aggregate their share of the 1 M rows, then merge. SQLite is single-threaded by design. DuckDB's vectorized pipeline is excellent, but at this scale the extra coordination overhead doesn't outrun PG's parallel plan. Q2 is selective enough (premium ∩ last-90-days) that PG's index intersection wins outright.

**EXPLAIN ANALYZE sketch (PostgreSQL):**
```
Finalize GroupAggregate  (cost=... rows=20)
  → Gather  (workers=2)
      → Partial HashAggregate  (key: u.country)
          → Hash Join  (hash cond: o.user_id = u.user_id)
              → Parallel Seq Scan on orders  (filter: status='completed')
              → Hash  → Seq Scan on users
```

### Q3 — SQLite wins

This is the OLTP shape: fetch one user's ~10 rows. Both PG and SQLite walk `idx_orders_user_id` and pull the rows — sub-millisecond. SQLite edges PG because there is **no process or connection overhead**; the library calls directly into the B-tree page cache. DuckDB has no secondary B-tree indexes on regular columns, so it scans all 1 M rows. 240 ms isn't DuckDB being slow — it's DuckDB being asked the wrong question.

### Q4 — DuckDB wins

Canonical columnar workload. DuckDB reads only the three columns it needs (`order_date`, `amount`, `status`). Its 256 KiB column blocks have **zone maps** (min/max per block), so it can skip entire blocks whose `status` range can't include `'completed'`. PostgreSQL and SQLite must walk every row in the heap to project the needed columns.

### Q5 — DuckDB wins

Window functions sort by `(user_id, order_date)` once, then compute `FIRST_VALUE` and `LEAD` in a single pass. DuckDB vectorizes the entire pipeline including the partition scan. PostgreSQL performs a single-threaded `WindowAgg` over a prior `Sort` node. If `work_mem` is insufficient, PG spills to disk (`external merge Disk` visible in `EXPLAIN ANALYZE`). SQLite window functions (added in 3.25) are correct but not performance-tuned.

---

## 8. Architectural Deep Dive

### How SQLite Reads a Page

```
Application: sqlite3_exec("SELECT * FROM students WHERE id=3")
    │
    ▼
SQLite B-tree layer (btree.c)
    │ compute page number for id=3 via root page → interior node → leaf
    ▼
Pager (pager.c) — SQLite's own buffer pool
    │ check pager cache (N pages in memory, controlled by PRAGMA cache_size)
    ├── Cache HIT  → return pointer to in-memory page
    └── Cache MISS → pread64(db_fd, page_buf, 4096, page_offset)
                     → OS page cache → disk if needed
```

### How PostgreSQL Reads a Page

```
Client: SELECT * FROM students WHERE id=3
    │
    ▼
Query planner → Index Scan on students_pkey
    │
    ▼
Executor → index_getrow() → heap_fetch()
    │
    ▼
Buffer Manager (shared_buffers pool)
    │ check shared buffer pool (all server processes share this RAM)
    ├── Buffer HIT  → pin buffer, return page pointer
    └── Buffer MISS → smgrread() → OS VFS → disk
                      load into shared buffer pool
                      → return pinned page
```

### How DuckDB Reads a Column Block

```
Query: SELECT SUM(amount) FROM orders WHERE status='completed'
    │
    ▼
DuckDB logical planner → physical plan: TableScan → Filter → Aggregate
    │
    ▼
Storage layer: ColumnSegment for 'status' column
    │ read zone map: min='cancelled', max='pending' → includes 'completed'? YES → read block
    ▼
Decompression (FSST for strings, LZ4 for numerics)
    │
    ▼
Vectorized execution: process 2048 rows per vector batch
    │ SIMD-accelerated filter on 'status' column
    │ SIMD-accelerated SUM on 'amount' column for matching rows
    ▼
Result
```

---

## 9. Honest Trade-offs

| Trade-off | PostgreSQL | SQLite | DuckDB |
|-----------|-----------|--------|--------|
| Setup complexity | High: server, pg_hba.conf, roles | None: just a file | Minimal: just a file |
| Concurrency (writes) | Excellent: MVCC, many writers | One writer at a time (WAL helps reads) | One writer at a time (file lock) |
| Concurrency (reads) | Excellent: snapshot isolation | Excellent in WAL mode | Single process |
| Connection overhead | Per-connection process (costs ~5 MB RSS) | None — in-process | None — in-process |
| Horizontal scaling | Citus, read replicas, logical replication | Not possible | Not possible |
| Analytical performance | Good (parallel seq scans) | Poor (row-store, single-threaded) | Excellent (columnar, SIMD) |
| OLTP performance | Excellent | Excellent for single-user | Poor (no secondary indexes) |
| Portability | Needs server running | Single file, zero deps | Single file + DuckDB lib |

---

## 10. When to Pick What

```
┌───────────────────────────────────────────────────────────┐
│ Decision flowchart                                         │
│                                                           │
│  Multiple concurrent writers?                             │
│    YES ──► PostgreSQL                                     │
│    NO  ──► Continue                                       │
│                                                           │
│  Analytics / aggregations / JOINs on millions of rows?    │
│    YES ──► DuckDB                                         │
│    NO  ──► Continue                                       │
│                                                           │
│  Single process, embedded, no server wanted?              │
│    YES ──► SQLite                                         │
│    NO  ──► PostgreSQL                                     │
└───────────────────────────────────────────────────────────┘
```

- **PostgreSQL** — Web backends, APIs, multi-user apps, anything needing row-level locking, MVCC isolation (REPEATABLE READ, SERIALIZABLE), extensions (PostGIS, pg_vector), auditing, roles/auth, replication.
- **SQLite** — Mobile apps, desktop apps, CLI tools, test databases, embedded systems, configuration files with relational structure, read-heavy single-user workloads.
- **DuckDB** — Python/R analytics notebooks, CSV/Parquet query engines, ETL pipelines, OLAP dashboards, data science workloads. Best as the read-side of a lambda architecture (PG writes, DuckDB queries).

---

## 11. How mmap Fits Into Each Engine

| Engine | mmap usage |
|--------|-----------|
| SQLite | Optional (`PRAGMA mmap_size`). When enabled, maps the `.db` file into the process address space. Reads become page faults rather than `pread()` syscalls — faster for sequential scans, no benefit for random page reads if the OS page cache is warm anyway. |
| PostgreSQL | Does NOT use mmap for its primary shared buffer pool (`shared_buffers`). The buffer pool is explicit shared memory (`shmget`/`mmap` of `/dev/zero`). WAL reads do use mmap. The deliberate avoidance lets PG control eviction precisely (it knows which pages are dirty and which are pinned by active transactions). |
| DuckDB | Uses mmap internally for its column blocks. Reads are served via memory access after the initial page fault; the OS handles cache eviction. |

---

## Key Takeaways

1. SQLite is a **library** — no daemon, no socket, no auth. It is the most widely deployed database engine in existence (every Android/iOS device has many SQLite databases).
2. PostgreSQL is a **server** — separate process, MVCC, full SQL, concurrent writers. The right default for any multi-user application.
3. DuckDB is a **columnar analytical engine** — reads only the columns it needs, compresses aggressively, vectorizes everything. Unbeatable for analytics on a single machine.
4. The right choice is always workload-dependent: OLTP point lookups, OLAP full-table aggregations, and embedded single-user access are three distinct design points that three different engines serve best.
5. `mmap` in SQLite trades `pread()` syscall overhead for page fault overhead — beneficial when the database is larger than the OS page cache and sequential access patterns dominate.
