# Database Internals Lab Report
### SQLite3 vs PostgreSQL — Storage, Memory, and Query Performance

---

## 1. Environment Setup

| Component | Details |
|-----------|---------|
| SQLite3 Database | `sakila.db` |
| PostgreSQL Database | `mydb` (table: `products`, 50,000 rows) |
| PostgreSQL Block Size | 8192 bytes |

---

## 2. SQLite3 Exploration

### 2.1 PRAGMA Commands

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size=60000000;
```

The `PRAGMA mmap_size=60000000` command sets memory-mapped I/O to ~60 MB, allowing SQLite to map the database file directly into virtual address space instead of copying data through the kernel buffer.

---

### 2.2 Query Timing — SELECT (Full Table Scan)

**Without `mmap`:**

| Run | Real Time | User Time | Sys Time |
|-----|-----------|-----------|----------|
| 1   | 0.008s    | 0.003929s | 0.002897s |
| 2   | 0.007s    | 0.004464s | 0.001835s |
| 3   | 0.006s    | 0.004085s | 0.001610s |

**With `mmap` (`PRAGMA mmap_size=60000000`):**

| Run | Real Time | User Time | Sys Time |
|-----|-----------|-----------|----------|
| 1 (cold) | 0.000s | 0.000060s | 0.000021s |
| 2   | 0.010s    | 0.004143s | 0.003327s |
| 3   | 0.007s    | 0.004140s | 0.001819s |
| 4   | 0.006s    | 0.004351s | 0.001795s |

**Observation:** The first `mmap` SELECT was near-instant (0.000s real) because the OS had already cached the file pages in memory. Subsequent runs showed similar performance to the non-mmap baseline, indicating diminishing returns once the data is already warm in the page cache.

---

### 2.3 Query Timing — JOIN

**Without `mmap`:**

| Run | Real Time | User Time | Sys Time |
|-----|-----------|-----------|----------|
| 1   | 0.000s    | 0.000231s | 0.000234s |
| 2   | 0.001s    | 0.000327s | 0.000294s |
| 3   | 0.000s    | 0.000313s | 0.000254s |

**With `mmap`:**

| Run | Real Time | User Time | Sys Time |
|-----|-----------|-----------|----------|
| 1   | 0.004s    | 0.000376s | 0.001633s |
| 2   | 0.003s    | 0.000296s | 0.001303s |
| 3   | 0.000s    | 0.000240s | 0.000231s |

**Observation:** JOIN queries were marginally *slower* on the first runs under `mmap`. This is likely due to the overhead of setting up memory-mapped regions for multiple table pages simultaneously. By the third run, performance was equivalent — showing mmap needs warm-up time.

---

### 2.4 Process Inspection

```bash
ps aux | grep sqlite
```

```
akshatsipany  15216  0.0  0.0  435310272  2816  s001  S+  11:00AM  0:00.15  sqlite3 sakila.db
```

SQLite3 runs as a single-process, in-process database engine. The process shows minimal CPU and memory usage (~2.8 MB RSS), consistent with SQLite's lightweight, embedded design philosophy.

---

## 3. PostgreSQL Exploration

### 3.1 Data Insertion

```sql
CREATE TABLE products (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    category TEXT,
    price FLOAT,
    stock INT,
    created_at TIMESTAMP DEFAULT now()
);

INSERT INTO products (name, category, price, stock)
SELECT
    'Product_' || i,
    (ARRAY['Electronics','Clothing','Books','Furniture','Sports'])[1 + (i % 5)],
    round((random() * 5000)::numeric, 2),
    (random() * 100)::INT
FROM generate_series(1, 50000) AS s(i);
-- INSERT 0 50000
-- Time: 142.640 ms
```

50,000 rows were generated using `generate_series` with randomized prices and stock values distributed evenly across 5 categories. The full bulk insert completed in **142.64 ms**.

---

### 3.2 Block Size

```sql
SHOW block_size;
```

| Parameter | Value | Query Time |
|-----------|-------|------------|
| `block_size` | 8192 bytes (8 KB) | 0.940 ms |

PostgreSQL's block size is fixed at compile time and cannot be changed at runtime. All heap pages, index pages, and visibility map pages use this fixed 8 KB unit. A larger block size reduces the number of I/O operations needed for large sequential scans but increases the minimum I/O granularity.

---

### 3.3 Table Storage Metadata

```sql
SELECT relpages FROM pg_class WHERE relname = 'products';
SELECT pg_size_pretty(pg_relation_size('products'));
SELECT relname, relpages, reltuples::bigint AS estimated_rows,
       pg_size_pretty(relpages::bigint * 8192) AS estimated_size
FROM pg_class WHERE relname = 'products';
```

| Metric | Value |
|--------|-------|
| Page Count (`relpages`) | 504 |
| Table Size (before index) | 4032 kB |
| Table Size (after index) | 5184 kB |
| Estimated Rows | 50,000 |

The table grew by ~1.15 MB after creating an index on `price`, reflecting the B-tree index storage overhead.

---

### 3.4 Memory Configuration

```sql
SHOW shared_buffers;
SHOW work_mem;
SHOW maintenance_work_mem;
SHOW effective_cache_size;
SHOW temp_buffers;
SHOW wal_buffers;
```

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `shared_buffers` | 128 MB | Shared in-memory cache for data pages |
| `work_mem` | 4 MB | Per-operation memory for sorts/hashes |
| `maintenance_work_mem` | 64 MB | Memory for VACUUM, CREATE INDEX |
| `effective_cache_size` | 4 GB | Planner's estimate of available OS cache |
| `temp_buffers` | 8 MB | Per-session temp table buffers |
| `wal_buffers` | 4 MB | Write-Ahead Log buffer |

---

### 3.5 Query Performance — Before Index

All queries against the `products` table (50,000 rows) used **Sequential Scans** before indexing.

```sql
EXPLAIN ANALYZE SELECT * FROM products WHERE price > 4500;
-- Seq Scan | Execution Time: 6.337 ms | Total: 22.616 ms

EXPLAIN ANALYZE SELECT * FROM products ORDER BY price DESC LIMIT 50;
-- Seq Scan + top-N heapsort | Execution Time: 11.955 ms | Total: 16.730 ms

EXPLAIN ANALYZE SELECT category, COUNT(*), ROUND(AVG(price)::numeric,2), SUM(stock)
FROM products GROUP BY category ORDER BY total_products DESC;
-- HashAggregate + Seq Scan | Execution Time: 18.774 ms | Total: 22.417 ms
```

---

### 3.6 Index Creation and Impact

```sql
CREATE INDEX idx_products_price ON products(price);
-- Time: 41.190 ms
```

**After index — `WHERE price > 4500` (selective, ~10% of rows):**

```
Bitmap Heap Scan → Bitmap Index Scan on idx_products_price
Execution Time: 4.283 ms  (was 6.337 ms — 32% faster)
Total Time:     7.448 ms  (was 22.616 ms — 67% faster)
```

**After index — `WHERE price > 500` (non-selective, ~90% of rows):**

```
Seq Scan (index was IGNORED by planner)
Execution Time: 12.579 ms
```

**Observation:** The PostgreSQL query planner correctly chose a Bitmap Index Scan for the selective query (`price > 4500`, ~4,967 rows) but fell back to a Sequential Scan for the non-selective query (`price > 500`, ~45,039 rows). This demonstrates cost-based optimization — scanning 90% of the table via an index is more expensive than a straight sequential scan.

---

### 3.7 PostgreSQL Process Architecture

```bash
ps aux
```

| Process | Role |
|---------|------|
| `postgres` (PID 1) | Postmaster — main supervisor process |
| `checkpointer` | Flushes dirty pages to disk |
| `background writer` | Incrementally writes shared buffers to disk |
| `walwriter` | Writes WAL records to disk |
| `autovacuum launcher` | Triggers VACUUM/ANALYZE automatically |
| `logical replication launcher` | Manages logical replication workers |
| `admin mydb ... idle` | Client connection handler (idle) |

PostgreSQL uses a **multi-process architecture** — each client connection spawns a separate backend process, and background daemons handle I/O, durability, and maintenance continuously.

---

## 4. SQLite3 vs PostgreSQL — Comparison

### 4.1 Page / Block Size

| Metric | SQLite3 | PostgreSQL |
|--------|---------|------------|
| Default Page Size | 4096 bytes (4 KB) | 8192 bytes (8 KB) |
| Configurable | Yes (`PRAGMA page_size`) | At compile time only |
| Unit Name | Page | Block / Page |

PostgreSQL uses larger 8 KB blocks, better suited for bulk sequential I/O on large datasets. SQLite's 4 KB default aligns with typical OS page sizes and is optimized for embedded, low-memory environments.

---

### 4.2 Page Count

| Metric | SQLite3 | PostgreSQL |
|--------|---------|------------|
| Inspected via | `PRAGMA page_count` | `pg_class.relpages` |
| Products table pages | — | 504 pages (~4 MB) |
| Index pages (price) | — | +~145 pages (~1.15 MB) |

---

### 4.3 Query Performance

| Query Type | SQLite3 (real time) | PostgreSQL (execution time) |
|------------|--------------------|-----------------------------|
| Full table scan | ~6–8 ms | ~6–13 ms (50K rows) |
| Filtered scan (selective) | N/A | 4.3 ms (with index) |
| Filtered scan (non-selective) | N/A | 12.6 ms (seq scan) |
| Aggregation (GROUP BY) | N/A | 18.8 ms |
| JOIN | ~0–1 ms | N/A |
| Bulk insert (50K rows) | N/A | 142.64 ms |

---

### 4.4 mmap Impact (SQLite3)

| Scenario | Behavior |
|----------|----------|
| First query with mmap (cold) | Near-zero real time — OS page cache hit |
| Subsequent queries with mmap | Comparable to non-mmap (no significant gain) |
| JOINs with mmap (first run) | Slightly slower — mmap setup overhead |
| JOINs with mmap (warm) | Equivalent to non-mmap |

**Key insight:** `mmap` in SQLite provides the most benefit when the database fits entirely in OS virtual memory and repeated reads avoid repeated syscall overhead. For small or already-cached databases, the difference is negligible. PostgreSQL manages its own shared buffer cache (`shared_buffers`) and relies on OS page cache via `effective_cache_size` hints — it does not expose `mmap` as a user-tunable setting.

---

### 4.5 Architecture Summary

| Aspect | SQLite3 | PostgreSQL |
|--------|---------|------------|
| Architecture | Embedded, single-file | Client-server, multi-process |
| Concurrency | Single writer, WAL mode for readers | Full MVCC, multiple concurrent writers |
| Process model | In-process library | Postmaster + per-connection backends |
| Memory management | `mmap`, page cache, explicit PRAGMA | Shared buffers, work_mem, OS cache |
| Best suited for | Embedded apps, mobile, local storage | Production workloads, multi-user apps |
| Index planner | Simple rule-based | Cost-based optimizer (uses statistics) |
| Observability | `PRAGMA` commands | `pg_class`, `EXPLAIN ANALYZE`, system views |

---

## 5. Commands Reference

```bash
# SQLite3
sqlite3 sakila.db
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size=60000000;
.timer on
SELECT * FROM users;
ps aux | grep sqlite

# PostgreSQL
\timing on
SHOW block_size;
SELECT relpages FROM pg_class WHERE relname = 'products';
SELECT pg_size_pretty(pg_relation_size('products'));
EXPLAIN ANALYZE SELECT * FROM products WHERE price > 4500;
CREATE INDEX idx_products_price ON products(price);
SHOW shared_buffers;
SHOW work_mem;
SHOW effective_cache_size;
```

---

## 6. Conclusions

1. **Page / Block Size:** PostgreSQL's fixed 8 KB block size (confirmed via `SHOW block_size`, returning `8192` in 0.940 ms) is better suited for large datasets and sequential I/O. SQLite's configurable 4 KB pages align with typical OS memory pages, optimizing for small embedded use cases.

2. **Data Insertion:** PostgreSQL inserted 50,000 rows in 142.64 ms using `generate_series`, demonstrating efficient bulk ingestion. The table occupied 504 pages (4032 kB) immediately after insertion, growing to 5184 kB once a B-tree index on `price` was added.

3. **mmap in SQLite:** Memory-mapped I/O reduces syscall overhead and benefits cold-start reads significantly. However, once the OS has cached the pages, subsequent queries see minimal improvement, and JOINs may even incur slight overhead on the first run.

4. **Index impact in PostgreSQL:** The cost-based planner uses indexes only when selectivity justifies it. A B-tree index on `price` gave a 67% end-to-end speedup for selective queries (`>4500`) but was correctly bypassed for non-selective ones (`>500`), where a sequential scan is cheaper.

5. **Architecture:** SQLite is a zero-configuration embedded engine ideal for single-user or mobile workloads. PostgreSQL's multi-process architecture with dedicated background workers (checkpointer, WAL writer, autovacuum) makes it far more capable for concurrent, production-grade workloads.
