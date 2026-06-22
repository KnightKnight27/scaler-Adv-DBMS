# Lab Assignment 2: Database Storage & Performance Analysis

**Name:** Ayush Kumar Patra
**Roll no.:** 24bcs10474

---

## 1. SQLite3 Exploration

### 1.1 Database File Info
A sample database `sample.db` was created with a `users` table containing 100,000 rows.

**Command:**
```powershell
ls -lh Lab 2/sample.db
```

**Observation:**
- **File Size:** 14.43 MB

### 1.2 PRAGMA Observations
Using the SQLite3 CLI to inspect internal storage parameters.

| Parameter | Value | Command |
|-----------|-------|---------|
| **Page Size** | 4096 bytes | `PRAGMA page_size;` |
| **Page Count** | 3694 | `PRAGMA page_count;` |
| **mmap_size** | 0 (Default) | `PRAGMA mmap_size;` |

### 1.3 Memory-Mapped I/O (mmap) Experiment
We compared query performance for `SELECT * FROM users;` with and without memory mapping.

| Configuration | Execution Time (Avg) |
|---------------|----------------------|
| **Without mmap** (mmap_size = 0) | 0.1089s |
| **With mmap** (mmap_size = 256MB) | 0.0996s |

**Observation:**
Enabling `mmap` reduced execution time by approximately **8.5%**. Memory mapping allows the OS to map the database file into the process's address space, reducing the number of system calls (`read()`) and avoiding extra copying between kernel and user space.

### 1.4 Process Monitoring
**Command:**
```powershell
Get-Process | Where-Object { $_.Name -like "*sqlite*" }
```
*(Note: Since SQLite3 is a library-based engine, it typically runs within the context of the calling application/process rather than as a standalone background service like PostgreSQL.)*

---

## 2. PostgreSQL (PSQL) Exploration

### 2.1 Database Info
*(Note: Analysis performed on a standard PostgreSQL 18 installation)*

| Parameter | Value | Command |
|-----------|-------|---------|
| **Page Size** | 8192 bytes (8KB) | `SHOW block_size;` |
| **Page Count** | ~1800 (for 100k rows) | `SELECT relpages FROM pg_class WHERE relname='users';` |

### 2.2 Performance Analysis
PostgreSQL uses a shared buffer cache rather than direct mmap for its primary storage engine, though it utilizes OS-level caching.

| Configuration | Execution Time (Avg) |
|---------------|----------------------|
| **Standard Query** | ~0.15s - 0.20s |

---

## 3. Comparison Report

| Feature | SQLite3 | PostgreSQL |
|---------|---------|------------|
| **Architecture** | Serverless (Library-based) | Client-Server |
| **Default Page Size** | 4096 bytes (4KB) | 8192 bytes (8KB) |
| **Page Count** | Higher (due to smaller page size) | Lower (due to larger page size) |
| **mmap Impact** | Significant performance boost | Managed via `shared_buffers` |
| **Query Performance** | Faster for simple reads | Optimized for concurrency |

### Analysis:
1. **Page Size:** PostgreSQL uses a larger default page size (8KB) compared to SQLite3 (4KB). This is optimized for server-grade workloads and larger row sizes.
2. **Performance:** SQLite3 is extremely fast for single-user, local read-heavy operations due to zero network overhead. PostgreSQL shines in multi-user environments with complex transactions.
3. **mmap vs Shared Buffers:** SQLite3's `mmap` is a simple way to leverage the OS page cache. PostgreSQL's `shared_buffers` is a more sophisticated, database-managed cache that provides better control over memory management in a multi-process environment.

---
*Created as part of the Advanced DBMS Lab tasks.*
