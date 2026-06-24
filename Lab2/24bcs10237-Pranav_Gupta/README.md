# Lab Assignment 2: SQLite3 and PostgreSQL Exploration

**Name:** Pranav Gupta  
**Roll No:** 24BCS10237  

---

## 1. SQLite3 Exploration

### Environment Setup
A sample database `sample.db` was created with a `users` table containing 100,000 rows. Each row consists of an `id`, `name`, `email`, and `bio`.

### File Size Observation
```bash
ls -lh Lab2/24bcs10237-Pranav_Gupta/data/sample.db
```
**Observation:** The file size is approximately **14 MB**.

### Page Information (PRAGMA Commands)
- **Page Size:** 4096 bytes (4 KB)
- **Page Count:** 3583
- **Calculated Size:** 3583 * 4096 = 14,675,968 bytes (~14 MB)

### Experimenting with mmap_size
- **Default mmap_size:** 0 (Disabled)
- **Changed mmap_size:** 268,435,456 (256 MB)

**Behavior Change:** 
Setting `mmap_size` allows SQLite to use memory-mapped I/O instead of standard `read()`/`write()` system calls. This can improve performance by allowing the OS to handle paging and reducing the overhead of copying data between kernel and user space.

### Query Performance Comparison
The command `time sqlite3 sample.db "SELECT * FROM users;" > /dev/null` was used.

| Mode | Execution Time (Real) |
|------|-----------------------|
| With mmap (256 MB) | 0.056s |
| Without mmap (0) | 0.053s |

**Note:** For a relatively small database (14 MB), the overhead of setting up the memory map can occasionally make it slightly slower than direct I/O, as seen in this trial. However, for larger-than-RAM databases, `mmap` typically shows significant gains.

### Process Monitoring
```bash
ps aux | grep sqlite
```
**Observation:** SQLite is an embedded database, so it runs within the process of the calling application (in this case, the `sqlite3` CLI). It does not run as a background service.

---

## 2. PostgreSQL (PSQL) Setup

### Environment Setup
A database `lab2_db` was created. A table `users` was populated with 100,000 rows using `generate_series`.

### Page Information
- **Page Size (Block Size):** 8192 bytes (8 KB)
- **Page Count (relpages):** 2632
- **Total Relation Size:** 23 MB

### Query Performance
```bash
time psql -U postgres -d lab2_db -c "SELECT * FROM users;" > /dev/null
```
**Execution Time (Real):** 0.462s

**Observation:** PostgreSQL has a higher execution time compared to SQLite3 for this simple scan. This is primarily due to:
1. **Client-Server Overhead:** PostgreSQL requires a socket connection and protocol parsing.
2. **ACID Compliance/MVCC:** PostgreSQL maintains more metadata per row for concurrent access control.
3. **Larger Row Size:** The default row overhead in PostgreSQL is higher than SQLite's minimalist format.

---

## 3. Comparison Report

| Metric | SQLite3 | PostgreSQL |
|--------|---------|------------|
| **Default Page Size** | 4096 bytes (4 KB) | 8192 bytes (8 KB) |
| **Page Count (100k rows)** | 3583 | 2632 |
| **Storage Efficiency** | Higher (~14 MB) | Lower (~23 MB) |
| **Query Performance** | Faster (0.05s) | Slower (0.46s) |
| **Architecture** | Serverless / Embedded | Client-Server |
| **mmap Impact** | Direct (via PRAGMA) | Managed by OS / Shared Buffers |

### Analysis
1. **Page Size:** PostgreSQL uses a larger page size (8KB) by default, which is optimized for enterprise workloads and larger data blocks. SQLite defaults to 4KB, matching the standard OS page size on most systems.
2. **Performance:** SQLite is significantly faster for simple, single-user read operations due to the lack of network overhead and its lightweight nature.
3. **Storage:** PostgreSQL uses more disk space for the same data because it stores additional information for Multi-Version Concurrency Control (MVCC) and has more structural overhead.
4. **mmap:** SQLite provides explicit control over memory mapping via `mmap_size`. PostgreSQL relies more on its `shared_buffers` and the OS kernel cache, though it can use huge pages for large memory allocations.
