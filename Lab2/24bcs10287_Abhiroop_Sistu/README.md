# Advanced DBMS Lab: Storage Engine Comparison (SQLite3 vs PostgreSQL)

**Name:** Abhiroop Sistu
**Role Number:** 10287

---

# 🎯 Objective

The objective of this lab is to explore the internal storage engines of SQLite3 and PostgreSQL. By analyzing disk layout, page structures, process architectures, and memory mapping (`mmap`), we can understand how different system architectures affect query processing and overall database performance.

---

# Part 1: SQLite3 Exploration

## 1. Setup and Disk Layout

I created a sample database named `school.db`, created a `students` table, and inserted 10,000 records to observe byte-level record encoding and disk layout.

### Command

```bash
ls -lh school.db
```

### Observation

```text
-rw-r--r-- 1 student user 196K May 9 23:00 school.db
```

SQLite stores the entire database, including tables and metadata, inside a single contiguous `.db` file. As rows are inserted, the file size grows dynamically.

---

## 2. Page Structure (Page Size and Count)

Databases divide files into fixed-size blocks called pages. SQLite uses a slotted page structure to organize records efficiently.

### Commands

```sql
PRAGMA page_size;
PRAGMA page_count;
```

### Observations

- `page_size = 4096 bytes (4 KB)`
- `page_count = 49`

SQLite defaults to a 4 KB page size. With 49 allocated pages, the database efficiently packs records using slotted pages to manage row pointers and free space.

---

## 3. Memory Mapping (`mmap`) and Query Performance

By default, databases use standard I/O system calls like `read()` to fetch pages into memory. `mmap` maps the database file directly into virtual memory, reducing data copying overhead.

### Checking Default mmap Setting

```sql
PRAGMA mmap_size;
```

### Output

```text
0
```

This indicates that `mmap` is disabled by default.

---

### Timing Without mmap

```bash
time sqlite3 school.db "SELECT * FROM students;" > /dev/null
```

### Result

```text
real: 0m0.065s
user: 0m0.012s
sys : 0m0.020s
```

---

### Timing With mmap Enabled

```bash
sqlite3 school.db "PRAGMA mmap_size = 268435456;"
time sqlite3 school.db "SELECT * FROM students;" > /dev/null
```

### Result

```text
real: 0m0.058s
user: 0m0.008s
sys : 0m0.015s
```

### Observation

Enabling `mmap` slightly reduced CPU time because it bypassed standard file buffering and minimized memory copying. The improvement was small because the dataset was already cached in memory.

---

## 4. System Architecture

### Command

```bash
ps aux | grep sqlite
```

### Observation

Only the interactive SQLite process was visible. No separate server daemon exists.

SQLite operates as an embedded database engine where the query processing system runs entirely inside the host application process.

---

# Part 2: PostgreSQL Setup & Exploration

## 1. Architecture and Setup

After installing PostgreSQL and creating a parallel database named `schooldb`, I inspected its process architecture.

### Command

```bash
ps aux | grep postgres
```

### Observation

Unlike SQLite, PostgreSQL uses a client-server architecture. Multiple background worker processes were visible, including:

- Checkpointer
- Background Writer
- WAL Writer
- Autovacuum Launcher

This architecture enables PostgreSQL to support high concurrency, MVCC (Multi-Version Concurrency Control), and complex query pipelines.

---

## 2. Page (Block) Structure

### Commands

```sql
SHOW block_size;

SELECT relpages
FROM pg_class
WHERE relname = 'students';
```

### Observations

- `block_size = 8192 bytes (8 KB)`
- `relpages = 64`

PostgreSQL uses an 8 KB default block size. Similar to SQLite, PostgreSQL also uses a slotted page architecture, but the larger page size is optimized for enterprise-scale workloads and sequential disk access.

---

## 3. Query Performance

I enabled timing inside the PostgreSQL console to measure execution speed.

### Commands

```sql
\timing

SELECT * FROM students;
```

### Observation

```text
Execution Time: ~4.120 ms
```

Query execution was extremely fast because PostgreSQL uses a dedicated shared memory buffer pool (`shared_buffers`) to cache frequently accessed pages.

---

# Part 3: Comparison Report

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| System Architecture | Embedded library (in-process) | Client-server daemon |
| Disk Layout | Single `.db` file | Multiple relation files |
| Page / Block Size | 4096 bytes (4 KB) | 8192 bytes (8 KB) |
| Page Layout | Slotted pages | Slotted pages |
| Memory Strategy | OS page cache / mmap | Dedicated `shared_buffers` |

---

# Detailed Analysis

## Page Size

SQLite uses 4 KB pages, which are efficient for embedded systems and mobile devices. PostgreSQL uses larger 8 KB blocks, reducing the number of disk I/O operations during large scans and analytical workloads.

Both databases rely on slotted pages to efficiently organize byte-level records inside pages.

---

## Page Count and Storage

PostgreSQL stores additional metadata for MVCC, such as transaction IDs, causing its storage footprint to differ from SQLite even for identical datasets.

SQLite generally packs records more tightly into smaller pages.

---

## Query Performance

For single-user workloads, SQLite performs extremely fast because there is no server communication overhead.

PostgreSQL introduces small inter-process communication overhead for lightweight queries, but it scales significantly better for concurrent multi-user environments.

---

## mmap Impact

In SQLite, enabling `mmap` allows database pages to be accessed directly through virtual memory instead of traditional `read()` system calls.

This reduces:

- CPU overhead
- Memory copying
- Kernel-user data transfers

The performance gain was small in this experiment because the dataset was already cached in RAM, but for larger databases and I/O-heavy workloads, `mmap` can provide noticeable improvements.

PostgreSQL uses a different caching strategy through `shared_buffers` rather than depending heavily on `mmap`.

---

# Conclusion

This experiment demonstrated the architectural and storage differences between SQLite3 and PostgreSQL.

SQLite is lightweight, simple, and ideal for embedded or single-user applications. PostgreSQL provides a robust client-server architecture optimized for scalability, concurrency, and enterprise workloads.

Although both systems use slotted page structures internally, their storage strategies, caching mechanisms, and execution architectures differ significantly based on their design goals.