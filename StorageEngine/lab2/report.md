# Lab 2 — DB Comparison Report: PostgreSQL vs SQLite vs MySQL

This report compares three databases on the same dataset and query set, with a focus on internal storage parameters and the effect of SQLite's `mmap_size` setting.

## Schema

- `customers(customer_id, first_name, last_name, email, phone_number, city, state, signup_date, customer_type, is_active)`
- `orders(order_id, customer_id, order_channel, store_id, order_source, order_date, order_timestamp, order_status, total_amount, payment_id)`
- `order_items(order_item_id, order_id, product_id, quantity, item_price, discount_amount, line_total)`
- Indexes on `orders(customer_id)`, `orders(order_date)`, `orders(order_status)`, `customers(city)`

## Data Volume

| Table | Rows |
|---|---|
| `customers` | 100,000 |
| `orders` | 500,000 |
| `order_items` | 1,500,873 |

## Queries

**Q1 — Filter + aggregate on orders:**
```sql
SELECT order_status, COUNT(*), SUM(total_amount)
FROM orders
WHERE order_date >= '2024-01-01'
GROUP BY order_status;
```

**Q2 — Join customers × orders, group by city:**
```sql
SELECT c.city, COUNT(o.order_id) AS order_count, AVG(o.total_amount) AS avg_amount
FROM customers c JOIN orders o ON c.customer_id = o.customer_id
GROUP BY c.city
ORDER BY order_count DESC LIMIT 10;
```

**Q3 — Join orders × order_items, filter by status, top 10 by total:**
```sql
SELECT o.order_id, SUM(oi.line_total) AS total
FROM orders o JOIN order_items oi ON o.order_id = oi.order_id
WHERE o.order_status = 'Delivered'
GROUP BY o.order_id
ORDER BY total DESC LIMIT 10;
```

---

## Storage & Internal Parameters

| Aspect | PostgreSQL | MySQL (InnoDB) | SQLite |
|---|---|---|---|
| Model | Server process, multi-user | Server process, multi-user | Embedded library, single-user |
| Storage layout | Multiple relation files per table + WAL in server data directory | Multiple `.ibd` files per table inside data directory | Single `.db` file |
| Page size | 8,192 bytes (compile-time default) | 16,384 bytes (InnoDB default) | 4,096 bytes (PRAGMA `page_size`) |
| Page count | ~33,280 (derived: 260 MB ÷ 8 KB) | ~14,425 (derived: 225.4 MB ÷ 16 KB) | 50,416 (PRAGMA `page_count`) |
| Memory tuning knob | `shared_buffers` (dedicated buffer pool) | `innodb_buffer_pool_size` (dedicated buffer pool) | `mmap_size` (OS-level memory-mapped I/O) |
| mmap used | No (uses shared_buffers instead) | No (uses buffer pool instead) | Yes (optional via PRAGMA) |
| DB size observed | 260 MB | 225.4 MB | 197 MB |
| Key table sizes | orders=94 MB, customers=17 MB, order_items=142 MB | total data+index=225.4 MB | single file 197 MB |
| Execution style | Cost-based planner, parallel execution, mixed workload | Cost-based planner (InnoDB), row-based, general purpose | B-tree, single-threaded, lightweight, local-first |
| Best fit | Multi-user transactional and mixed workloads | General-purpose server workloads, web apps | Simple local apps, zero-ops embedded storage |

---

## Query Timing Results

> All times are wall-clock averages over warm-cache runs (runs 2–3 of each set of 3).
> PostgreSQL times from `\timing`; MySQL and SQLite times from `time` shell wrapper.

### Q1 — Filter + aggregate on orders

| Run | PostgreSQL | MySQL | SQLite (mmap=0) | SQLite (mmap=256MB) |
|---|---|---|---|---|
| Run 1 | 31.256 ms | 720 ms | 110 ms | 110 ms |
| Run 2 | 33.042 ms | 712 ms | 111 ms | 110 ms |
| Run 3 | 30.542 ms | 767 ms | 111 ms | 119 ms |
| **Avg** | **31.6 ms** | **733 ms** | **111 ms** | **113 ms** |

### Q2 — Join customers × orders, group by city

| Run | PostgreSQL | MySQL | SQLite (mmap=0) | SQLite (mmap=256MB) |
|---|---|---|---|---|
| Run 1 | 73.881 ms | 1,049 ms | 720 ms | 790 ms |
| Run 2 | 72.422 ms | 1,043 ms | 748 ms | 714 ms |
| Run 3 | 69.322 ms | 1,048 ms | 735 ms | 727 ms |
| **Avg** | **71.9 ms** | **1,047 ms** | **734 ms** | **744 ms** |

### Q3 — Join orders × order_items (heavy join, 500k × 1.5M rows)

| | PostgreSQL | MySQL | SQLite |
|---|---|---|---|
| Avg time | **549 ms** | **3,090 ms** | **> 20 min (timed out, killed)** |

---

## SQLite `mmap_size` Experiment

The lab specifically explores the effect of enabling memory-mapped I/O in SQLite.

| Setting | Value |
|---|---|
| Baseline | `PRAGMA mmap_size = 0` (disabled) |
| Experiment | `PRAGMA mmap_size = 268435456` (256 MB) |

| Query | mmap=0 | mmap=256MB | Change |
|---|---|---|---|
| Q1 | 111 ms | 113 ms | ≈ no change |
| Q2 | 734 ms | 744 ms | ≈ no change |

**Observation:** Enabling `mmap_size` produced negligible improvement on macOS. This is because macOS aggressively caches file pages in the OS page cache (unified buffer cache). SQLite's `mmap_size` lets it avoid the `read()` syscall by mapping file pages directly into the process address space, but when the OS has already cached those pages, the gain is minimal — the data is warm either way.

On systems with a cold page cache or with I/O-bound workloads on Linux, `mmap_size` can produce measurable speedups by eliminating the extra copy from kernel page cache into userspace buffers.

---

## Comparison Summary

| Aspect | PostgreSQL | MySQL | SQLite |
|---|---|---|---|
| Q1 avg time | **31.6 ms** | 733 ms | 111 ms |
| Q2 avg time | **71.9 ms** | 1,047 ms | 734 ms |
| Q3 avg time | **549 ms** | 3,090 ms | > 20 min (timeout) |
| DB size | 260 MB | 225.4 MB | **197 MB** |
| Page size | 8 KB | 16 KB | 4 KB |
| Memory model | Dedicated shared_buffers | Dedicated buffer pool | OS mmap / page cache |

---

## Experiment Observations

- **PostgreSQL was the fastest on all three queries** by a significant margin. Its cost-based planner, parallel execution, and shared_buffers pool make it highly efficient even on large joins.
- **MySQL was consistently 10–20× slower than PostgreSQL** on these queries. InnoDB's row-by-row execution and higher per-connection overhead show up clearly at this data volume.
- **SQLite was fast on Q1 (111 ms)** — faster than MySQL — because the query is simple and SQLite processes it entirely in-process with zero network overhead. However, it cannot compete on complex joins.
- **SQLite completely failed on Q3** (orders × order_items: 500k × 1.5M rows). After 20+ minutes of CPU time it was killed. Without a server-side query optimizer and with no parallelism, SQLite does a nested-loop join with no hash join fallback, making this class of query impractical.
- **`mmap_size` had no measurable effect on macOS** because the OS page cache already warms the data. The PRAGMA is more impactful on Linux or with cold-start benchmarks.
- **SQLite's smaller file size (197 MB vs 260 MB for PostgreSQL)** is because PostgreSQL stores MVCC visibility info (dead tuple versions, transaction IDs) alongside live data, adding overhead that SQLite — with no concurrent writers — avoids entirely.
- **The right tool depends on the workload:** SQLite for embedded/local/zero-ops use, MySQL for general web apps, PostgreSQL for serious multi-user transactional or mixed analytical workloads.
