# SQLite3 vs PostgreSQL Exploration Report

## Submitted By

**Name:** Penta Guna Sai Kumar
**Role Number:** 24BCS10070

---

# 1. SQLite3 Exploration

## Introduction

SQLite is a lightweight, serverless relational database management system that stores the entire database within a single file. This experiment explores SQLite's storage architecture, page management, memory-mapped I/O (mmap), and query performance.

---

## Environment Setup

* SQLite Version: **3.53.1**
* Operating System: **Windows**
* Database File: **test.db**

A sample database was created along with a `users` table containing mock data for performance evaluation.

---

## Database File Inspection

```bash
dir test.db
```

### Observation

* SQLite stores the complete database inside a single file (`test.db`).
* The file size increases as records are inserted.
* No separate server process is required for database operations.

---

## PRAGMA Analysis

### Page Size

```sql
PRAGMA page_size;
```

**Output:** 4096 bytes

### Page Count

```sql
PRAGMA page_count;
```

**Output:** 47 pages

### Estimated Database Size

Database Size = Page Size × Page Count

= 4096 × 47

= 192,512 bytes (~188 KB)

### Observation

SQLite organizes data into fixed-size pages. The total database size can be estimated by multiplying the page size by the number of allocated pages.

---

## Memory-Mapped I/O (mmap) Experiment

### Default mmap Setting

```sql
PRAGMA mmap_size;
```

**Output:** 0

This indicates that memory-mapped I/O is disabled by default.

### Enabling mmap

```sql
PRAGMA mmap_size = 268435456;
```

This configures SQLite to use up to **256 MB** of memory-mapped space.

---

## Query Performance Evaluation

### Without mmap

```powershell
Measure-Command { .\sqlite3 test.db "SELECT * FROM users;" }
```

**Execution Time:** ~209 ms

### With mmap Enabled

```powershell
Measure-Command { .\sqlite3 test.db "SELECT * FROM users;" }
```

**Execution Time:** ~53 ms

### Performance Improvement

Percentage Improvement:

((209 - 53) / 209) × 100 ≈ 74.6%

### Observation

Enabling memory-mapped I/O reduced query execution time significantly, resulting in approximately **75% faster performance**.

---

## SQLite Key Findings

* SQLite follows an embedded architecture.
* No dedicated database server is required.
* Data is stored in a single portable file.
* Memory-mapped I/O substantially improves read performance.
* Suitable for lightweight applications and local storage.
* Concurrency support is limited compared to enterprise-grade database systems.

---

# 2. PostgreSQL Exploration

## Introduction

PostgreSQL is an advanced open-source relational database system that follows a client-server architecture. It is designed for high concurrency, scalability, and reliability.

---

## Environment Setup

* PostgreSQL installed locally
* Sample database created
* Same `users` table used for comparison

---

## Storage Analysis

### Block Size

```sql
SHOW block_size;
```

**Output:** 8192 bytes (8 KB)

### Page Count

```sql
SELECT relpages
FROM pg_class
WHERE relname = 'users';
```

**Output:** 41 pages

### Note

Initially, the `relpages` value returned 0 because PostgreSQL statistics had not yet been updated. Running:

```sql
ANALYZE users;
```

updated the internal statistics, allowing accurate page information to be retrieved.

---

## Query Performance Evaluation

```sql
\timing
SELECT * FROM users;
```

### Results

| Execution  | Time      |
| ---------- | --------- |
| First Run  | 16.131 ms |
| Second Run | 5.083 ms  |

### Observation

The second execution was significantly faster because PostgreSQL cached frequently accessed data in memory using shared buffers and the operating system page cache.

---

## PostgreSQL Key Findings

* Uses a dedicated client-server architecture.
* Supports high concurrency and multiple simultaneous users.
* Optimized for large-scale applications.
* Includes advanced caching mechanisms.
* Provides strong support for transactions, indexing, and scalability.

---

# 3. SQLite vs PostgreSQL Comparison

| Feature            | SQLite3                      | PostgreSQL              |
| ------------------ | ---------------------------- | ----------------------- |
| Architecture       | Embedded                     | Client-Server           |
| Storage            | Single File                  | Multiple Managed Files  |
| Page Size          | 4096 Bytes                   | 8192 Bytes              |
| Page Count         | 47                           | 41                      |
| Query Optimization | mmap-based                   | Internal Caching        |
| Concurrency        | Limited                      | High                    |
| Scalability        | Small to Medium Applications | Enterprise Applications |
| Setup Complexity   | Easy                         | Moderate                |
| Resource Usage     | Low                          | Higher                  |

---

# Key Insights

1. SQLite is highly efficient for lightweight and standalone applications.
2. PostgreSQL provides better scalability and concurrency for production environments.
3. Memory-mapped I/O significantly enhances SQLite read performance.
4. PostgreSQL achieves performance improvements through sophisticated caching mechanisms.
5. The choice between SQLite and PostgreSQL depends on application size, user load, and scalability requirements.

---

# Conclusion

### SQLite is Best Suited For

* Mobile applications
* Embedded systems
* Desktop software
* Local data storage
* Low-concurrency environments

### PostgreSQL is Best Suited For

* Enterprise applications
* Web platforms
* Multi-user systems
* High-transaction workloads
* Large-scale production deployments

Overall, SQLite offers simplicity and excellent performance for small-scale applications, whereas PostgreSQL provides superior scalability, reliability, and concurrency management for modern production systems.

---

# Bonus Observation

The mmap experiment demonstrated a reduction in query execution time from approximately **209 ms to 53 ms**, representing a performance improvement of nearly **75%**.

Similarly, PostgreSQL query execution improved from **16.131 ms to 5.083 ms** on repeated execution due to effective utilization of shared buffers and operating system caching, minimizing disk access and improving response time.
