# Lab Report: SQLite3 vs PostgreSQL Storage Internals and Query Performance

**Course:** Advanced Database Systems Lab 2  
**Name:** Dhruv Soni  
**Roll Number:** 10205  
**Environment:** Docker Desktop on Windows (Ubuntu 22.04 container for SQLite3, postgres:15 image for PostgreSQL)

---

## Part 1: SQLite3 Exploration

### 1. Setup and Dataset

I set up SQLite3 inside an Ubuntu 22.04 Docker container. For the dataset I used the **Northwind** sample database, which models a small trading company with tables for customers, orders, products, and suppliers.

```bash
docker pull ubuntu:22.04
docker run -it --name sqlite-lab ubuntu:22.04 bash
apt-get update && apt-get install -y sqlite3 wget
wget https://raw.githubusercontent.com/jpwhite3/northwind-SQLite3/main/dist/northwind.db
```

### 2. File System Inspection

```bash
ls -lh northwind.db
```

**Observation:** The file was about **608 KB** on disk. Pretty small considering it has multiple relational tables with foreign key relationships across them.

### 3. Database Metadata via PRAGMA

I used the PRAGMA commands to check how SQLite stores data internally.

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA journal_mode;
```

**Results:**

| PRAGMA | Value |
|---|---|
| `page_size` | 4096 bytes (4 KB) |
| `page_count` | 152 pages |
| `journal_mode` | delete (default) |

**Analysis:**  
SQLite splits the database file into 4096 byte pages. This number is not arbitrary. It matches the default memory page size on Linux, so every read or write the database does fits exactly into one OS memory page. No padding, no splitting across pages. If you multiply it out: 152 pages x 4096 bytes = 622,592 bytes, which is close to the 608 KB we saw. The gap is from file headers and partially used pages.

### 4. mmap Experiment and Query Benchmarking

I timed a full scan on the `Orders` table with and without mmap.

**Without mmap:**

```bash
time sqlite3 northwind.db "SELECT * FROM Orders;"
```

| Run | Time |
|---|---|
| Run 1 (Cold) | 42ms |
| Run 2 (Warm) | 19ms |
| Run 3 (Warm) | 17ms |

**With mmap (25 MB mapped):**

```bash
time sqlite3 northwind.db "PRAGMA mmap_size=25000000; SELECT * FROM Orders;"
```

| Run | Time |
|---|---|
| Run 1 | 21ms |
| Run 2 | 15ms |
| Run 3 | 15ms |

**Observation:**  
The big drop from Run 1 to Run 2 without mmap is just the OS page cache kicking in. After the first read the file stays in RAM so the next query does not touch the disk at all. Turning on mmap helped a little on warm runs, probably by cutting down on system call overhead, but the difference was small. That makes sense since the whole database is only 608 KB and fits in cache easily. mmap would matter a lot more on a database that is several GB in size.

I also ran a grouped aggregate to test something more compute-heavy:

```bash
time sqlite3 northwind.db "SELECT CustomerID, COUNT(*) FROM Orders GROUP BY CustomerID ORDER BY COUNT(*) DESC LIMIT 10;"
```

**Time: ~23ms (warm)**

No real difference with or without mmap here. The bottleneck was the grouping logic, not I/O.

### 5. Process Monitoring

```bash
ps aux | grep sqlite
```

**Observation:**  
The sqlite3 process only showed up while the query was running. Once it finished the process disappeared completely. There is no background server or daemon at all. SQLite is just a library that runs inside whatever program calls it. This is great for simple use cases since there is nothing to set up or manage. The downside is only one process can write to the file at a time since locking happens at the file level.

---

## Part 2: PostgreSQL Exploration

### 1. Setup and Dataset

I ran the postgres:15 Docker image and connected with psql.

```bash
docker pull postgres:15
docker run --name pg-lab -e POSTGRES_PASSWORD=secret -d postgres:15
docker exec -it pg-lab psql -U postgres
```

I created a database with an `orders` table and inserted 100,000 rows using generate_series:

```sql
CREATE DATABASE labtest;
\c labtest

CREATE TABLE orders (
    order_id    SERIAL PRIMARY KEY,
    customer_id INT,
    product_id  INT,
    quantity    INT,
    unit_price  NUMERIC(10,2),
    status      TEXT,
    created_at  DATE
);

INSERT INTO orders (customer_id, product_id, quantity, unit_price, status, created_at)
SELECT
    (random() * 999 + 1)::INT,
    (random() * 299 + 1)::INT,
    (random() * 50 + 1)::INT,
    round((random() * 500 + 5)::NUMERIC, 2),
    CASE WHEN random() < 0.7 THEN 'completed'
         WHEN random() < 0.9 THEN 'pending'
         ELSE 'cancelled' END,
    DATE '2022-01-01' + (random() * 730)::INT
FROM generate_series(1, 100000);
```

### 2. Storage Internals

```sql
SHOW block_size;
SELECT relpages, pg_size_pretty(pg_relation_size('orders')) AS table_size
FROM pg_class
WHERE relname = 'orders';
```

**Results:**

| Metric | Value |
|---|---|
| `block_size` | 8192 bytes (8 KB) |
| `relpages` | 1063 blocks |
| Table size | ~8.3 MB |

**Analysis:**  
PostgreSQL uses 8 KB blocks, which is double what SQLite uses. On a server doing big sequential scans this means fewer I/O operations since each read grabs more data at once. For our 100,000-row table: 1063 blocks x 8192 = about 8.7 MB, which is close to the 8.3 MB shown. The small difference comes from MVCC metadata that Postgres stores with every row for transaction visibility.

### 3. Query Performance and Index Impact

```sql
\timing on

SELECT * FROM orders LIMIT 200;
-- Time: 0.821 ms

SELECT status, SUM(quantity * unit_price) AS revenue
FROM orders
GROUP BY status;
-- Time: 28.4 ms

SELECT * FROM orders WHERE unit_price > 450.00;
-- Time: 18.7 ms
```

I checked what the query planner was doing:

```sql
EXPLAIN ANALYZE SELECT * FROM orders WHERE unit_price > 450.00;
```

**Output:** Seq Scan on orders. It read all 1063 blocks since there was no index and it estimated roughly 10% of rows would match.

After adding an index:

```sql
CREATE INDEX idx_orders_price ON orders(unit_price);

SELECT * FROM orders WHERE unit_price > 450.00;
-- Time: 3.2 ms
```

**Observation:**  
The index brought query time down from 18.7ms to 3.2ms, about 6x faster. One thing I found interesting: when I ran a query with a lower threshold like `unit_price > 10.00`, Postgres ignored the index completely and did a sequential scan instead. I verified this with EXPLAIN. When a query is going to return most of the table anyway, jumping through the index and fetching individual heap pages is actually slower than reading everything in order. Postgres figures this out automatically using table statistics.

### 4. Process Monitoring

```bash
ps aux | grep postgres
```

**Observation:**  
Postgres had 8 processes running from the start, even before any queries:
- `postgres` (main process that handles connections)
- `checkpointer` (writes dirty pages to disk on a schedule)
- `background writer` (does smaller writes between checkpoints)
- `walwriter` (keeps the write-ahead log updated)
- `autovacuum launcher` (cleans up dead rows from updates and deletes)
- `logical replication launcher`

This is very different from SQLite. All of this exists to support multiple users writing safely at the same time and recovering from crashes. For a web app this is necessary, but it would be overkill for something like a local desktop tool.

---

## Part 3: Comparison Analysis

### Summary Table

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| **Page/Block Size** | 4 KB (4096 bytes) | 8 KB (8192 bytes) |
| **Pages in Lab Data** | 152 pages (~608 KB) | 1063 blocks (~8.3 MB) |
| **Architecture** | In-process library | Client-server daemon |
| **Background Processes** | 0 | 8 |
| **Write Concurrency** | One writer at a time (file lock) | Multiple writers (row-level MVCC) |
| **Memory Mapping** | Manual via PRAGMA mmap_size | Managed via shared_buffers |
| **Query Planner** | Simple rule-based | Cost-based with statistics |
| **Crash Recovery** | Optional WAL via journal_mode | WAL always enabled |

### Detailed Breakdown

**Page Size:**  
SQLite uses 4 KB pages to match Linux's memory page size, which works well for its in-process design. PostgreSQL uses 8 KB blocks because it is built for servers doing large scans where reading bigger chunks at once is more efficient.

**mmap vs shared_buffers:**  
Both are ways to keep frequently used data in memory. SQLite's mmap_size is set manually and tells the OS to map the database file directly into the process's memory space. PostgreSQL manages its own buffer pool through shared_buffers, which is shared across all active connections. The PostgreSQL approach works better under concurrent load since it does not rely on each process managing its own mapping.

**Concurrency:**  
SQLite only allows one writer at a time. If two processes try to write at the same time, one has to wait. PostgreSQL uses MVCC so readers and writers mostly do not block each other. Each transaction sees its own consistent snapshot of the data. For any multi-user application this is basically a hard requirement.

**Query Planning:**  
SQLite uses a simple planner that follows basic rules. PostgreSQL runs ANALYZE periodically to collect statistics on column distributions and uses those to estimate the cost of different query plans before picking one. This is why it decided to skip the index for a query returning most of the table, which is something SQLite's planner would not do.

**Which to use:**  
SQLite is the right pick for local tools, config storage, mobile apps, and testing. Zero setup, no background processes. PostgreSQL makes sense for web backends and APIs where multiple users are writing concurrently or where you need reliable crash recovery.

---

## Part 4: Commands Reference

### SQLite3

```bash
# Docker setup
docker pull ubuntu:22.04
docker run -it --name sqlite-lab ubuntu:22.04 bash
apt-get update && apt-get install -y sqlite3 wget

# File inspection
ls -lh northwind.db

# Benchmarking
time sqlite3 northwind.db "SELECT * FROM Orders;"
time sqlite3 northwind.db "PRAGMA mmap_size=25000000; SELECT * FROM Orders;"

# Process check
ps aux | grep sqlite
```

```sql
-- Metadata
PRAGMA page_size;
PRAGMA page_count;
PRAGMA journal_mode;
PRAGMA mmap_size;

-- Queries
.tables
SELECT * FROM Orders LIMIT 20;
SELECT CustomerID, COUNT(*) FROM Orders GROUP BY CustomerID ORDER BY COUNT(*) DESC LIMIT 10;
```

### PostgreSQL

```bash
# Docker setup
docker pull postgres:15
docker run --name pg-lab -e POSTGRES_PASSWORD=secret -d postgres:15
docker exec -it pg-lab psql -U postgres

# Process check
ps aux | grep postgres
```

```sql
-- Storage metadata
SHOW block_size;
SELECT relpages, pg_size_pretty(pg_relation_size('orders')) FROM pg_class WHERE relname = 'orders';

-- Benchmarking
\timing on
SELECT * FROM orders LIMIT 200;
SELECT status, SUM(quantity * unit_price) AS revenue FROM orders GROUP BY status;
SELECT * FROM orders WHERE unit_price > 450.00;

-- Execution plan
EXPLAIN ANALYZE SELECT * FROM orders WHERE unit_price > 450.00;

-- Index
CREATE INDEX idx_orders_price ON orders(unit_price);
SELECT * FROM orders WHERE unit_price > 450.00;
```