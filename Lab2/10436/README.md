# Lab 2 : SQLite3 vs PostgreSQL: Storage Internals & Query Performance

**Course:** Advanced Database Systems : Lab 2\
**Name:** Romit Raj Sahu\
**Environment:** Fedora Linux (VM), SQLite 3.51.2, PostgreSQL 18.3\
**Datasets:**

- SQLite : Northwind Database (business ERP dataset : customers, orders, employees, products, suppliers), 24MB
- PostgreSQL : Synthetic `orders` table, 50,000 rows generated via `generate_series`

---

## Part 1 : SQLite3

### Dataset Overview

The Northwind database is a classic business dataset originally from Microsoft Access. It contains tables for customers, orders, order details, employees, products, suppliers, shippers, and territories : making it well suited for join-heavy and aggregation queries.

```bash
sqlite3 northwind.db
.tables
```

```
Alphabetical list of products   Orders
Categories                      Order Details
Customers                       Employees
Suppliers                       Products
Territories                     Shippers
...
```

---

### File Size on Disk

```bash
ls -lh northwind.db
```

```
-rw-r--r--. 1 romit romit 24M May 9 21:51 northwind.db
```

The entire database : all tables, indexes, and metadata : lives in a single 24MB file. This is a fundamental SQLite property: one database = one file.

---

### Storage Internals : PRAGMA Commands

PRAGMA commands are special instructions you can send to SQLite to read or change its internal settings.

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA journal_mode;
PRAGMA cache_size;
PRAGMA mmap_size;
```

| PRAGMA       | Value  | What it means                                                                                                                             |
| ------------ | ------ | ----------------------------------------------------------------------------------------------------------------------------------------- |
| page_size    | 4096   | Each page is 4096 bytes (4KB). SQLite stores all data in fixed-size chunks called pages.                                                  |
| page_count   | 6031   | There are 6031 pages in this database. 4096 × 6031 = ~24MB (matches file size)                                                            |
| journal_mode | delete | On every write, SQLite creates a temporary `-journal` file as a backup. On commit, it deletes it. This is the classic rollback mechanism. |
| cache_size   | -2000  | Negative value means KB. SQLite keeps up to 2MB of pages in memory to avoid hitting disk repeatedly.                                      |
| mmap_size    | 0      | Memory mapping is OFF by default. SQLite uses `read()` syscalls to load pages into its own memory.                                        |

The page size of 4KB is not random : it matches the default memory page size of the Linux kernel. When the OS loads data into memory, it does so in 4KB chunks. SQLite aligning to this means one SQLite page = one OS memory page, with no wasted space.

---

### Timing Queries : mmap OFF vs ON

**What is mmap?**
Normally, when SQLite reads a page from disk, the OS loads it into kernel memory first, then copies it again into SQLite's own memory space. That is two copies. With mmap (memory mapping), SQLite maps the database file directly into its address space : so it reads straight from the OS memory without the extra copy. Fewer copies = faster reads.

#### Simple Query : `SELECT * FROM Orders`

```sql
.timer on
PRAGMA mmap_size=0;
SELECT * FROM Orders;
```

```
Run Time: real 0.196649  user 0.029943  sys 0.023793
```

```sql
PRAGMA mmap_size=268435456;
SELECT * FROM Orders;
```

```
Run Time: real 0.116023  user 0.022901  sys 0.018527
```

| Mode     | real   | user   | sys    |
| -------- | ------ | ------ | ------ |
| mmap OFF | 0.196s | 0.029s | 0.023s |
| mmap ON  | 0.116s | 0.022s | 0.018s |

mmap made the query almost **2x faster** on a simple full table scan. The gain comes from eliminating the kernel-to-user memory copy on every page read.

---

#### Heavy Query : JOIN Across 3 Tables with GROUP BY

```sql
SELECT c.CompanyName, COUNT(o.OrderID) as TotalOrders,
       SUM(od.UnitPrice * od.Quantity) as TotalRevenue
FROM Customers c
JOIN Orders o ON c.CustomerID = o.CustomerID
JOIN [Order Details] od ON o.OrderID = od.OrderID
GROUP BY c.CustomerID
ORDER BY TotalRevenue DESC
LIMIT 20;
```

| Mode     | real   | user   | sys    |
| -------- | ------ | ------ | ------ |
| mmap ON  | 0.275s | 0.142s | 0.122s |
| mmap OFF | 0.258s | 0.127s | 0.122s |

Here the difference was negligible. This query is **CPU-bound** : it spends most of its time doing joins, grouping, and sorting rather than reading data from disk. mmap only helps when the bottleneck is I/O. When the CPU is the bottleneck, removing the copy step saves almost nothing.

**Key finding: mmap is not a universal speedup : it specifically helps I/O-heavy workloads.**

---

### Process Architecture : `ps aux`

```bash
sqlite3 northwind.db "SELECT COUNT(*) FROM Orders;" &
ps aux | grep sqlite
```

```
romit  5270  0.0  0.0  231452  2584  pts/0  S+  21:58  0:00  grep --color=auto sqlite
```

Only the `grep` command itself appeared : SQLite finished and vanished before `ps` could even catch it. There is **no SQLite server, no daemon, no background process**. SQLite is a C library embedded directly into whatever program calls it. When the call is done, it is gone. Zero overhead, but also zero concurrency : only one writer can hold the file lock at a time.

---

## Part 2 : PostgreSQL

### Dataset Setup

```sql
CREATE DATABASE lab2;
\c lab2

CREATE TABLE orders (
    id SERIAL PRIMARY KEY,
    customer_name TEXT NOT NULL,
    product TEXT NOT NULL,
    city TEXT,
    amount FLOAT,
    quantity INTEGER,
    created_at TIMESTAMP DEFAULT now()
);

INSERT INTO orders (customer_name, product, city, amount, quantity)
SELECT
    'Customer_' || i,
    'Product_' || (i % 100),
    (ARRAY['Mumbai','Delhi','Bangalore','Chennai','Hyderabad'])[1 + (i % 5)],
    ROUND((random() * 10000)::numeric, 2),
    (i % 50) + 1
FROM generate_series(1, 50000) AS s(i);
-- INSERT 0 50000
```

---

### Storage Internals : Blocks

```sql
SHOW block_size;
SELECT relpages FROM pg_class WHERE relname = 'orders';
SELECT pg_size_pretty(pg_relation_size('orders'));
SELECT pg_size_pretty(pg_total_relation_size('orders'));
```

| Metric                 | Value   | What it means                                                                                                                     |
| ---------------------- | ------- | --------------------------------------------------------------------------------------------------------------------------------- |
| block_size             | 8192    | Each block is 8192 bytes (8KB). PostgreSQL's equivalent of SQLite's page. Fixed at compile time : cannot be changed per database. |
| relpages               | 569     | There are 569 blocks used by the orders table. 8192 × 569 = ~4552KB (matches below)                                               |
| pg_relation_size       | 4552 kB | Raw table data size.                                                                                                              |
| pg_total_relation_size | 5704 kB | Total size including primary key index. The ~1MB difference is the cost of maintaining the index.                                 |

---

### Query Timings

```sql
\timing on
SELECT * FROM orders LIMIT 100;                                                    -- 1.535 ms
SELECT city, COUNT(*), ROUND(AVG(amount)::numeric, 2) FROM orders GROUP BY city;  -- 6.139 ms
SELECT * FROM orders WHERE amount > 9500;                                          -- 6.789 ms
SELECT * FROM orders ORDER BY amount DESC LIMIT 50;                                -- 10.191 ms
```

---

### EXPLAIN ANALYZE : The Query Planner in Action

`EXPLAIN ANALYZE` is a PostgreSQL command that shows exactly how the database executed a query internally : what strategy it chose, how many rows it scanned, and how long each step took.

#### Filter Query : Before Index

```sql
EXPLAIN ANALYZE SELECT * FROM orders WHERE amount > 9500;
```

```
Seq Scan on orders  (cost=0.00..1194.00 rows=2543 width=56)
                    (actual time=0.015..5.363 rows=2523 loops=1)
  Filter: (amount > '9500'::double precision)
  Rows Removed by Filter: 47477
  Buffers: shared hit=569
Execution Time: 5.568 ms
```

PostgreSQL read all 50,000 rows to find the 2,523 that matched. With no index on `amount`, that was the only option. All 569 pages were already in shared_buffers so no disk reads were needed.

#### After Creating an Index

```sql
CREATE INDEX idx_orders_amount ON orders(amount);
-- Time: 31.712 ms

EXPLAIN ANALYZE SELECT * FROM orders WHERE amount > 9500;
```

```
Bitmap Heap Scan on orders  (actual time=0.302..0.831 rows=2523 loops=1)
  ->  Bitmap Index Scan on idx_orders_amount
        Index Cond: (amount > '9500'::double precision)
Execution Time: 0.908 ms
```

|                               | Execution Time |
| ----------------------------- | -------------- |
| Without index (Seq Scan)      | 5.568ms        |
| With index (Bitmap Heap Scan) | 0.908ms        |

**~6x faster.** PostgreSQL first used the index to build a map of matching row locations, then fetched only those rows : instead of reading everything.

#### Index Intentionally Ignored

```sql
EXPLAIN ANALYZE SELECT * FROM orders WHERE amount > 100;
```

```
Seq Scan on orders  (actual time=0.013..4.420 rows=49495 loops=1)
  Filter: (amount > '100'::double precision)
  Rows Removed by Filter: 505
Execution Time: 6.977 ms
```

Even though an index exists on `amount`, PostgreSQL ignored it and did a Seq Scan. Because `amount > 100` matches ~99% of the table, using the index would require randomly jumping to 49,495 different locations across the table : which is slower than simply reading everything sequentially. PostgreSQL's cost-based query planner calculated both options and chose the cheaper one. **PostgreSQL does not blindly use indexes : it uses them only when they actually help.**

---

### Memory Configuration

```sql
SHOW shared_buffers;        -- 128MB
SHOW work_mem;              -- 4MB
SHOW maintenance_work_mem;  -- 64MB
SHOW effective_cache_size;  -- 4GB
```

| Parameter            | Value | Role                                                                                                                    |
| -------------------- | ----- | ----------------------------------------------------------------------------------------------------------------------- |
| shared_buffers       | 128MB | PostgreSQL's own page cache shared across all connections.                                                              |
| work_mem             | 4MB   | Memory budget per sort or hash operation. Exceeding this spills to disk.                                                |
| maintenance_work_mem | 64MB  | Memory for heavy operations like VACUUM and CREATE INDEX.                                                               |
| effective_cache_size | 4GB   | A hint to the query planner about how much the OS page cache is likely holding. Influences index vs seq scan decisions. |

---

### MVCC : Dead Rows in Practice

MVCC stands for Multi-Version Concurrency Control. When PostgreSQL updates a row, it never overwrites the old version. Instead it writes a new version and marks the old one as dead. Both versions exist on disk simultaneously : this allows a long-running SELECT to keep seeing the old data while an UPDATE writes new data, without either blocking the other.

```sql
UPDATE orders SET amount = amount + 100 WHERE city = 'Mumbai';
-- UPDATE 10000

SELECT relname, n_live_tup, n_dead_tup
FROM pg_stat_user_tables WHERE relname = 'orders';
```

```
 relname | n_live_tup | n_dead_tup
---------+------------+------------
 orders  |      50000 |      10000
```

After updating 10,000 Mumbai rows, `n_dead_tup = 10000`. Those are the old row versions sitting as ghost data on disk.

```sql
VACUUM orders;

SELECT relname, n_live_tup, n_dead_tup
FROM pg_stat_user_tables WHERE relname = 'orders';
```

```
 relname | n_live_tup | n_dead_tup
---------+------------+------------
 orders  |      50000 |          0
```

VACUUM reclaimed all 10,000 dead row slots. If autovacuum never ran on a write-heavy table, dead rows would accumulate indefinitely : pages would fill with ghost data, scans would waste time reading useless rows, and performance would degrade over time.

---

### Process Architecture : `ps aux`

```bash
ps aux | grep postgres
```

```
postgres  5820  /usr/bin/postgres -D /var/lib/pgsql/data    (postmaster)
postgres  5822  postgres: logger
postgres  5823  postgres: io worker 0
postgres  5824  postgres: io worker 2
postgres  5825  postgres: io worker 1
postgres  5826  postgres: checkpointer
postgres  5827  postgres: background writer
postgres  5829  postgres: walwriter
postgres  5830  postgres: autovacuum launcher
postgres  5831  postgres: logical replication launcher
postgres  6155  postgres: postgres lab2 [local] idle        (our connection)
```

10 processes running before a single query even starts. Each exists for a specific reason:

- **postmaster** : The parent process. Manages everything, spawns all other processes.
- **logger** : Writes PostgreSQL logs to disk.
- **io worker 0/1/2** : Handle asynchronous I/O operations in the background.
- **checkpointer** : Periodically flushes modified pages from shared_buffers to disk. Without this, a crash would require replaying the entire WAL from the last checkpoint.
- **background writer** : Proactively writes modified pages ahead of checkpoints so query execution is not suddenly interrupted by I/O bursts.
- **walwriter** : Flushes the Write-Ahead Log to disk. Every committed transaction is in the WAL before the client gets a response : this is what makes commits survive crashes.
- **autovacuum launcher** : Automatically spawns workers to VACUUM and ANALYZE tables that accumulate dead rows.
- **logical replication launcher** : Manages replication to replica databases.

---

## Part 3 : Comparison

### Page Size & Storage

| Metric                  | SQLite3                            | PostgreSQL                                      |
| ----------------------- | ---------------------------------- | ----------------------------------------------- |
| Page / Block Size       | 4096 bytes (4KB)                   | 8192 bytes (8KB)                                |
| Page Count              | 6031 pages                         | 569 blocks                                      |
| Dataset                 | Northwind (~24MB, multiple tables) | 50,000 rows synthetic orders                    |
| Table Size on Disk      | 24MB (entire DB in one file)       | 4552 kB (relation) / 5704 kB (total with index) |
| Page size configurable? | Yes, per database at creation time | No, fixed at compile time                       |

### Query Performance

| Query                     | SQLite (mmap OFF) | SQLite (mmap ON) | PostgreSQL          |
| ------------------------- | ----------------- | ---------------- | ------------------- |
| Simple SELECT (full scan) | 0.196s            | 0.116s           | 1.535ms (LIMIT 100) |
| JOIN + GROUP BY           | 0.258s            | 0.275s           | 6.139ms             |
| Filter without index      | 0.196s            | 0.116s           | 5.568ms             |
| Filter with index         | N/A               | N/A              | 0.908ms             |

### mmap Impact (SQLite)

| Workload                                 | mmap OFF | mmap ON | Speedup    |
| ---------------------------------------- | -------- | ------- | ---------- |
| Simple full scan (SELECT \* FROM Orders) | 0.196s   | 0.116s  | ~1.7x      |
| Heavy JOIN + GROUP BY                    | 0.258s   | 0.275s  | negligible |

mmap helps I/O-bound queries by eliminating the kernel-to-user memory copy. It makes no difference for CPU-bound queries like complex joins and aggregations where the bottleneck is computation, not data reading.

### Architecture

| Aspect               | SQLite3                                       | PostgreSQL                                                  |
| -------------------- | --------------------------------------------- | ----------------------------------------------------------- |
| Process model        | In-process library, zero background processes | postmaster + 9 background processes (idle)                  |
| Concurrency          | One writer at a time (file-level lock)        | Many concurrent writers via MVCC                            |
| Memory mapping       | Manual via `PRAGMA mmap_size` per connection  | Implicit via `shared_buffers` (128MB, always on)            |
| Dead rows on UPDATE  | No concept of dead rows                       | Writes new row version, marks old as dead                   |
| Dead row cleanup     | N/A                                           | VACUUM (manual or autovacuum)                               |
| Index usage          | Basic                                         | Cost-based planner : ignores index when seq scan is cheaper |
| File layout          | One `.db` file for the entire database        | One file per relation under the data directory              |
| Write durability     | Rollback journal (delete mode)                | WAL + checkpointer + background writer                      |
| Background processes | 0                                             | 9+ (always running)                                         |

---

## Analysis

### Why the page size difference makes sense

SQLite's 4KB page aligns with the Linux kernel's memory page size : built for embedded and lightweight use where alignment with the OS matters most. PostgreSQL's 8KB block is a deliberate trade-off for server workloads: larger blocks mean fewer I/O operations per table scan, and with 128MB of shared_buffers available anyway, the extra memory cost per block is irrelevant.

### mmap: when it helps and when it doesn't

The honest result from the mmap experiment: on a simple full scan, mmap gave ~1.7x speedup by eliminating the per-page kernel-to-user memory copy. But on a heavy JOIN with GROUP BY, mmap made no difference because the CPU was the bottleneck, not I/O. mmap is not a universal fix : it is specifically an I/O optimization.

### The index decision PostgreSQL made correctly

The `amount > 100` query matching ~99% of the table was deliberately ignored by the index : and that is the right call. Randomly jumping between index pages and heap pages for 49,495 rows is slower than reading the entire table sequentially. PostgreSQL's cost-based planner estimated both plans and chose the cheaper one. SQLite has a simpler rule-based planner and does not make this kind of cost calculation.

### MVCC overhead is the price of concurrency

10,000 dead rows from a single UPDATE is a lot of ghost data. But that is the cost of MVCC : readers never block writers and writers never block readers, because old versions stay alive until no active transaction needs them. SQLite avoids this entirely, but the trade-off is one writer at a time, always. For a personal app or embedded use: totally fine. For any backend with concurrent writes: not viable.

### Architecture is the root of everything

Every performance characteristic in this lab traces back to one fact : SQLite is a library, PostgreSQL is a server. SQLite runs inside your process: no network, no IPC, no daemon overhead, but no concurrency and no isolation between the engine and the application. PostgreSQL runs as its own process ecosystem: every connection crosses a process boundary, background daemons run continuously, but in return you get MVCC, crash recovery via WAL, a cost-based query planner, and the ability to serve many clients simultaneously.

---

## Commands Reference

```bash
# SQLite
sqlite3 northwind.db
.tables
PRAGMA page_size;
PRAGMA page_count;
PRAGMA journal_mode;
PRAGMA cache_size;
PRAGMA mmap_size;
PRAGMA mmap_size=268435456;
.timer on
SELECT * FROM Orders;
ps aux | grep sqlite

# PostgreSQL
sudo -i -u postgres
psql
\c lab2
SHOW block_size;
SELECT relpages FROM pg_class WHERE relname = 'orders';
SELECT pg_size_pretty(pg_relation_size('orders'));
SELECT pg_size_pretty(pg_total_relation_size('orders'));
\timing on
EXPLAIN ANALYZE SELECT * FROM orders WHERE amount > 9500;
CREATE INDEX idx_orders_amount ON orders(amount);
SHOW shared_buffers;
SHOW work_mem;
UPDATE orders SET amount = amount + 100 WHERE city = 'Mumbai';
SELECT relname, n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname = 'orders';
VACUUM orders;
ps aux | grep postgres
```
