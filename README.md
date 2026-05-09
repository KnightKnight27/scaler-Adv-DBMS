# Database Internals: A Comparative Analysis
### SQLite3 vs PostgreSQL — Storage, Memory, and Query Performance

---

## 1. Experimental Environment

The laboratory environment was configured with the following parameters to ensure a controlled comparison between the two database engines.

| Component | Details |
|-----------|---------|
| SQLite3 Database | `sakila.db` |
| PostgreSQL Database | `mydb` (table: `products`, 50,000 rows) |
| PostgreSQL Block Size | 8192 bytes |

---

## 2. SQLite3 Exploration

### 2.1 Memory Mapping and Configuration

To inspect and modify SQLite's internal behavior, the following commands were utilized:

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size=60000000;

```

The application of `PRAGMA mmap_size=60000000` enables memory-mapped I/O for approximately 60 MB, allowing the database to map its file directly into the virtual address space instead of copying data through the kernel buffer.

---

### 2.2 Query Timing — SELECT (Full Table Scan)

**Without `mmap`:**

| Run | Real Time | User Time | Sys Time |
| --- | --- | --- | --- |
| 1 | 0.008s | 0.003929s | 0.002897s |
| 2 | 0.007s | 0.004464s | 0.001835s |
| 3 | 0.006s | 0.004085s | 0.001610s |

**With `mmap` (`PRAGMA mmap_size=60000000`):**

| Run | Real Time | User Time | Sys Time |
| --- | --- | --- | --- |
| 1 (cold) | 0.000s | 0.000060s | 0.000021s |
| 2 | 0.010s | 0.004143s | 0.003327s |
| 3 | 0.007s | 0.004140s | 0.001819s |
| 4 | 0.006s | 0.004351s | 0.001795s |

**Observation:** The initial `mmap` SELECT was nearly instantaneous because the OS had already pre-cached the file pages. Subsequent runs yielded performance levels similar to the baseline, suggesting that `mmap` benefits plateau once the data is warm in the page cache.

---

### 2.3 Query Timing — JOIN

**Without `mmap`:**

| Run | Real Time | User Time | Sys Time |
| --- | --- | --- | --- |
| 1 | 0.000s | 0.000231s | 0.000234s |
| 2 | 0.001s | 0.000327s | 0.000294s |
| 3 | 0.000s | 0.000313s | 0.000254s |

**With `mmap`:**

| Run | Real Time | User Time | Sys Time |
| --- | --- | --- | --- |
| 1 | 0.004s | 0.000376s | 0.001633s |
| 2 | 0.003s | 0.000296s | 0.001303s |
| 3 | 0.000s | 0.000240s | 0.000231s |

**Observation:** JOIN operations were marginally slower on the first runs with `mmap`. This suggests overhead in managing memory-mapped regions for multiple table pages concurrently. By the third run, performance stabilized, indicating `mmap` requires a warm-up period.

---

### 2.4 Process Inspection

A process check via `ps aux | grep sqlite` revealed that SQLite3 operates as a single-process, embedded engine. The resource footprint was minimal (~2.8 MB RSS), confirming its lightweight design philosophy.

---

## 3. PostgreSQL Exploration

### 3.1 Data Insertion

The `products` table was populated with 50,000 records using the following script:

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

```

This bulk insertion was highly efficient, completing in **142.64 ms**.

---

### 3.2 Block Size

PostgreSQL relies on a fixed block size, determined during compilation.

| Parameter | Value | Query Time |
| --- | --- | --- |
| `block_size` | 8192 bytes (8 KB) | 0.940 ms |

All heap, index, and visibility map pages utilize this fixed 8 KB unit; while larger blocks optimize sequential I/O for massive datasets, they increase the minimum I/O threshold.

---

### 3.3 Table Storage Metadata

| Metric | Value |
| --- | --- |
| Page Count (`relpages`) | 504 |
| Table Size (before index) | 4032 kB |
| Table Size (after index) | 5184 kB |
| Estimated Rows | 50,000 |

The addition of a B-tree index on `price` increased storage by ~1.15 MB, representing the structure's overhead.

---

### 3.4 Memory Configuration

| Parameter | Value | Purpose |
| --- | --- | --- |
| `shared_buffers` | 128 MB | Central data page cache |
| `work_mem` | 4 MB | Memory for individual sort/hash tasks |
| `maintenance_work_mem` | 64 MB | Memory for index builds and VACUUM |
| `effective_cache_size` | 4 GB | Planner’s view of available OS cache |
| `temp_buffers` | 8 MB | Per-session temporary table buffers |
| `wal_buffers` | 4 MB | Write-Ahead Log buffer |

---

### 3.5 Query Performance — Before Index

Prior to indexing, all queries required **Sequential Scans**.

* **Filter (`price > 4500`)**: Execution Time: 6.337 ms
* **Order By (`price DESC`)**: Execution Time: 11.955 ms
* **Aggregation (`GROUP BY`)**: Execution Time: 18.774 ms

---

### 3.6 Index Creation and Impact

```sql
CREATE INDEX idx_products_price ON products(price);

```

**Selective Query (`price > 4500`, ~10% of rows):**

* The planner switched to a **Bitmap Index Scan**.
* Execution Time: 4.283 ms (32% faster than Sequential Scan).

**Non-Selective Query (`price > 500`, ~90% of rows):**

* The planner used a **Sequential Scan**, ignoring the index.
* Execution Time: 12.579 ms

**Observation:** PostgreSQL’s cost-based optimizer correctly identifies that scanning 90% of a table via an index is less efficient than a direct scan.

---

### 3.7 PostgreSQL Process Architecture

PostgreSQL employs a multi-process architecture where each client connection spawns a backend process, supported by background daemons:

| Process | Role |
| --- | --- |
| `postgres` (PID 1) | Postmaster supervisor |
| `checkpointer` | Flushes dirty pages to disk |
| `background writer` | Incrementally writes shared buffers |
| `walwriter` | Writes WAL records |
| `autovacuum launcher` | Manages automatic maintenance |

---

## 4. SQLite3 vs PostgreSQL — Comparison

### 4.1 Page / Block Size

| Metric | SQLite3 | PostgreSQL |
| --- | --- | --- |
| Default Size | 4096 bytes (4 KB) | 8192 bytes (8 KB) |
| Configurable | Yes (`PRAGMA page_size`) | At compile time only |

PostgreSQL’s 8 KB blocks are better suited for large-scale sequential I/O, while SQLite’s 4 KB pages align with standard OS page sizes for embedded environments.

---

### 4.2 Query Performance Summary

| Query Type | SQLite3 (Real Time) | PostgreSQL (Execution Time) |
| --- | --- | --- |
| Full Table Scan | ~6–8 ms | ~6–13 ms |
| Selective Filter | N/A | 4.3 ms (Index used) |
| Aggregation | N/A | 18.8 ms |
| Bulk Insert (50K) | N/A | 142.64 ms |

---

### 4.3 Architecture Summary

| Aspect | SQLite3 | PostgreSQL |
| --- | --- | --- |
| Architecture | Embedded, single-file | Client-server, multi-process |
| Concurrency | Single writer | Full MVCC |
| Memory | `mmap`, page cache | Shared buffers, OS cache |
| Best Suited For | Mobile, local storage | Production workloads |

---

## 5. Conclusions

1. **I/O Efficiency:** PostgreSQL’s 8 KB block size is optimized for massive datasets, while SQLite’s 4 KB pages suit smaller, embedded use cases.
2. **Bulk Ingestion:** PostgreSQL handled 50,000 rows in 142.64 ms, demonstrating efficient ingestion for high-volume data.
3. **mmap Dynamics:** In SQLite, `mmap` significantly benefits cold-start reads but provides diminishing returns once pages are cached by the OS.
4. **Planner Intelligence:** PostgreSQL's cost-based planner avoids index usage for non-selective queries, preventing the overhead of "unhelpful" index scans.
5. **Scale:** SQLite is an ideal zero-config engine for single-user apps, whereas PostgreSQL’s specialized workers make it the standard for multi-user production environments.


