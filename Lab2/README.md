# Lab 2: SQLite3 vs PostgreSQL Comparison

**Roll Number:** `24BCS10230`  
**Name:** `Parth Taneja`

---

# Objective

The goal of this lab is to explore and compare SQLite3 and PostgreSQL in terms of:

- Storage architecture
- Page/block organization
- Query execution performance
- Memory mapping behavior (`mmap`)
- Process architecture

The experiments were performed using a dataset containing 500,000 customer records.

---

# 1. SQLite3 Exploration

## Creating the Database

Start SQLite3:

```bash
sqlite3 lab2.sqlite
```

Create the table:

```sql
CREATE TABLE "customers"(
  "Index" TEXT,
  "Customer Id" TEXT,
  "First Name" TEXT,
  "Last Name" TEXT,
  "Company" TEXT,
  "City" TEXT,
  "Country" TEXT,
  "Phone 1" TEXT,
  "Phone 2" TEXT,
  "Email" TEXT,
  "Subscription Date" TEXT,
  "Website" TEXT
);
```

Import CSV data:

```sql
.mode csv
.import customers-500000.csv customers
```

---

## Inspecting File Size

Command used:

```bash
ls -lh lab2.sqlite
```

Output:

```bash
-rw-r--r--@ 1 parthtaneja  staff    89M May  9 15:12 lab2.sqlite
```

Observation:

- SQLite stores the entire database inside a single file.
- The database size is approximately **89 MB**.

---

## Finding Page Size

Command:

```sql
PRAGMA page_size;
```

Output:

```text
4096
```

Observation:

- SQLite uses a default page size of **4096 bytes (4 KB)**.

---

## Finding Page Count

Command:

```sql
PRAGMA page_count;
```

Output:

```text
22703
```

Estimated database size:

```text
4096 × 22703 = 92,991,488 bytes ≈ 89 MB
```

Observation:

- The calculated size closely matches the actual file size reported by the operating system.

---

## Exploring mmap_size

Command:

```sql
PRAGMA mmap_size;
```

Output:

```text
0
```

Observation:

- A value of `0` means memory-mapped I/O is disabled.

---

## Query Timing Without mmap

Command:

```bash
time sqlite3 lab2.sqlite "PRAGMA mmap_size=0; SELECT count(*) FROM customers;" > /dev/null
```

Output:

```text
0.104 total
```

---

## Query Timing With mmap Enabled

Command:

```bash
time sqlite3 lab2.sqlite "PRAGMA mmap_size=268435456; SELECT count(*) FROM customers;" > /dev/null
```

Output:

```text
0.025 total
```

Observation:

- Enabling `mmap` reduced query execution time noticeably.
- With memory mapping enabled, SQLite can access database pages directly through virtual memory mapping, reducing copying overhead and system call overhead.

Note:

- Actual performance improvements may vary depending on caching and operating system behavior.

---

## Checking SQLite Process Architecture

Command:

```bash
ps aux | grep sqlite3
```

Output:

```bash
parthtaneja      19611   0.0  0.0 410060064    240   ??  R     2:56PM   0:00.00 grep sqlite3
```

Observation:

- No standalone SQLite server process exists.
- SQLite operates as an embedded library inside the application process.

---

# 2. PostgreSQL Exploration

## Finding PostgreSQL Block Size

Command:

```sql
SELECT current_setting('block_size');
```

Output:

```text
8192
```

Observation:

- PostgreSQL uses the term **block** instead of page.
- Default PostgreSQL block size is **8192 bytes (8 KB)**.

---

## Exploring PostgreSQL Buffer Management

Command:

```sql
SHOW shared_buffers;
```

Output:

```text
128MB
```

Observation:

- PostgreSQL does not expose a direct equivalent of SQLite's `mmap_size`.
- Instead, PostgreSQL primarily relies on:
  - `shared_buffers`
  - Operating system page cache

`shared_buffers` defines the amount of shared memory allocated for caching database pages.

---

## PostgreSQL Process Architecture

Command:

```bash
ps aux | grep postgres
```

Output:

```bash
parthtaneja      15017   0.0  0.1 435497456   5408   ??  S     2:46PM   0:00.08 /opt/homebrew/opt/postgresql@14/bin/postgres -D /opt/homebrew/var/postgresql@14
parthtaneja      15024   0.0  0.0 435493136   1312   ??  Ss    2:46PM   0:00.56 postgres: checkpointer
parthtaneja      15025   0.0  0.0 435493136    912   ??  Ss    2:46PM   0:00.03 postgres: background writer
parthtaneja      15026   0.0  0.0 435493136    704   ??  Ss    2:46PM   0:00.03 postgres: walwriter
parthtaneja      15027   0.0  0.0 435493136   1456   ??  Ss    2:46PM   0:00.05 postgres: autovacuum launcher
parthtaneja      15028   0.0  0.0 435347728    832   ??  Ss    2:46PM   0:00.18 postgres: stats collector
```

Observation:

- PostgreSQL runs as a dedicated database server.
- Multiple background worker processes handle:
  - Write-ahead logging (WAL)
  - Checkpointing
  - Automatic vacuuming
  - Statistics collection
  - Background page writing

---

# 3. SQLite3 vs PostgreSQL Comparison

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | Embedded library | Client-server DBMS |
| Storage | Single database file | Multiple managed database files |
| Default Page/Block Size | 4 KB | 8 KB |
| Memory Mapping | Optional via `mmap_size` | No direct equivalent |
| Caching Mechanism | OS page cache + optional mmap | `shared_buffers` + OS cache |
| Process Model | No separate server | Dedicated server with worker processes |
| Best Use Case | Lightweight/local applications | Concurrent multi-user systems |

---

# Conclusion

The experiments demonstrate clear architectural differences between SQLite3 and PostgreSQL.

SQLite3 is lightweight, simple, and embedded directly into applications. It uses a single-file database design and supports optional memory-mapped I/O for performance improvements.

PostgreSQL, on the other hand, is a full-fledged client-server database system designed for scalability, concurrency, and advanced database management. It uses larger default block sizes and maintains multiple background worker processes for database operations.

The mmap experiment also showed that enabling memory mapping in SQLite can improve query execution time for repeated scans on cached datasets.