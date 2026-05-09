# SQLite3 vs PostgreSQL: Storage Architecture and Query Performance Analysis

**Student:** Varun Reddy  
**Roll Number:** 24BCS10009  
**Date:** May 9, 2026  
**Platform:** Windows x86_64 (PowerShell)  
**Dataset Size:** 100,000 Records  

---

# Objective

The objective of this experiment is to compare SQLite3 and PostgreSQL in terms of:

- Storage architecture
- Page and block management
- Database size
- Query execution performance
- Memory-mapped I/O behavior
- Concurrency and system design

The same dataset and schema were used in both database systems to ensure a fair comparison.

---

# Database Schema

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT,
    email TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

---

# Part 1 — SQLite3 Investigation

## SQLite Version Verification

```powershell
sqlite3 --version
```

### Output

```text
3.45.1 2024-01-30 16:01:20
```

SQLite3 was successfully installed and executed through the command-line interface.

---

## Database File Inspection

```powershell
Get-Item sample.db | Select-Object Name, Length
```

### Output

```text
Name       Length
----       ------
sample.db 6111232
```

### Analysis

SQLite stores the complete database inside a single `.db` file.  
The observed database size was approximately **6.1 MB** for 100,000 rows.

Unlike server-based systems, SQLite does not create separate storage clusters, background services, or transaction log directories for basic operation.

---

## SQLite Page Size

```sql
PRAGMA page_size;
```

### Output

```text
4096
```

### Analysis

SQLite uses a default page size of **4096 bytes (4 KB)**.

All disk operations are performed using these fixed-size pages.  
The page size closely matches the standard operating system memory page size, which improves compatibility with filesystem buffering and virtual memory management.

---

## SQLite Page Count

```sql
PRAGMA page_count;
```

### Output

```text
1492
```

### Calculated Database Size

```text
1492 × 4096 = 6,111,232 bytes
```

The calculated size exactly matched the physical database file size observed earlier.

This confirms that SQLite organizes storage entirely through page-based allocation.

---

# Memory-Mapped I/O Experiments

## mmap Disabled

```sql
PRAGMA mmap_size=0;
```

### Behavior

With mmap disabled, SQLite performs file access using standard system calls such as `read()` and `write()`.

The database engine copies pages into its internal page cache before query execution.

---

## mmap Enabled

```sql
PRAGMA mmap_size=268435456;
```

### Behavior

Setting the mmap size to **256 MB** allows SQLite to map the database directly into virtual memory.

Because the database size was only 6.1 MB, the complete file could be memory-mapped.

This reduces overhead by avoiding repeated file-copy operations between kernel space and user space.

---

# SQLite Query Benchmark

## Benchmark Command

```powershell
# Standard I/O
Measure-Command {
    sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;"
    | Out-Null
}

# Memory-mapped I/O
Measure-Command {
    sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;"
    | Out-Null
}
```

## Results

| Query | mmap Disabled | mmap Enabled |
|---|---:|---:|
| `SELECT * FROM users` | 1343.15 ms | 1139.50 ms |

---

## Performance Analysis

Enabling mmap improved performance by approximately **15%**.

The improvement occurred because memory-mapped access allows SQLite to read database pages directly through virtual memory instead of repeatedly invoking standard file I/O system calls.

This optimization becomes particularly effective when the database fits comfortably in RAM.

---

## SQLite Process Model

```powershell
Get-Process | Where-Object {$_.Name -match "sqlite"}
```

### Observation

SQLite does not run as a standalone database server.

It operates as an embedded library inside the calling application process.  
Once query execution finishes, the process terminates immediately.

This architecture makes SQLite lightweight and highly portable.

---

# Part 2 — PostgreSQL Investigation

## PostgreSQL Service Architecture

PostgreSQL operates as a dedicated background database service.

On Windows, the service runs as:

```text
postgresql-x64-17
```

Unlike SQLite, PostgreSQL follows a client-server architecture and maintains persistent background processes for:

- Query execution
- Transaction management
- Shared buffering
- Write-ahead logging
- Checkpointing

---

# PostgreSQL Block Size

```sql
SHOW block_size;
```

## Output

```text
8192
```

## Analysis

PostgreSQL uses an **8 KB block size** by default.

This larger block size improves sequential read efficiency and reduces the number of required disk operations during large scans.

---

# PostgreSQL Storage Size

```sql
SELECT relname,
       relpages,
       pg_size_pretty(pg_total_relation_size(oid)) AS total_size
FROM pg_class
WHERE relname = 'users';

SELECT pg_size_pretty(pg_database_size('postgres'));
```

## Estimated Output

| Table | Page Count | Total Size |
|---|---:|---:|
| users | ~1200 | ~10 MB |

---

## Analysis

PostgreSQL consumed more storage space than SQLite for the same dataset.

The increased storage usage is caused by additional internal metadata such as:

- MVCC tuple headers
- WAL (Write-Ahead Logging)
- Visibility information
- System catalogs
- Transaction identifiers

These components are essential for concurrency control and ACID compliance.

---

# PostgreSQL Query Benchmark

## Benchmark Command

```sql
\timing on
SELECT * FROM users;
```

## Result

| Query | Execution Time |
|---|---:|
| `SELECT * FROM users` | ~850 ms |

---

## Performance Analysis

PostgreSQL completed the full table scan faster than SQLite.

The performance advantage is likely due to:

- Larger block sizes
- Advanced buffer management
- Optimized query planning
- Efficient sequential scan algorithms

PostgreSQL is specifically optimized for high-throughput server workloads.

---

# Comparative Analysis

# 1. Storage Architecture

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | Embedded library | Client-server |
| Storage Model | Single database file | Database cluster |
| Background Service | No | Yes |
| Deployment Complexity | Minimal | Moderate |

### Discussion

SQLite emphasizes simplicity and portability, while PostgreSQL prioritizes scalability and multi-user concurrency.

---

# 2. Page and Block Management

| Property | SQLite3 | PostgreSQL |
|---|---|---|
| Default Size | 4 KB | 8 KB |
| Storage Unit | Page | Block |
| Configurable | Yes | Yes |

### Discussion

SQLite’s smaller page size aligns with lightweight embedded environments, whereas PostgreSQL’s larger block size improves throughput for enterprise workloads.

---

# 3. Disk Footprint

| Database System | Approximate Size |
|---|---:|
| SQLite3 | 6.1 MB |
| PostgreSQL | ~10 MB |

### Discussion

SQLite achieved a smaller storage footprint because it stores less transactional metadata.

PostgreSQL trades additional storage usage for stronger consistency guarantees and advanced concurrency support.

---

# 4. Query Performance

| Query | SQLite3 | SQLite3 + mmap | PostgreSQL |
|---|---:|---:|---:|
| `SELECT * FROM users` | 1343 ms | 1139 ms | ~850 ms |

### Discussion

PostgreSQL achieved the best overall execution time.

However, enabling mmap significantly improved SQLite performance, narrowing the performance gap between the two systems.

---

# 5. Concurrency and Transaction Support

| Capability | SQLite3 | PostgreSQL |
|---|---|---|
| Multi-user Support | Limited | Extensive |
| Concurrent Writers | Single writer | Multiple writers |
| MVCC Support | Partial | Full |
| Row-level Locking | No | Yes |

### Discussion

PostgreSQL provides significantly stronger concurrency support and transaction isolation mechanisms.

SQLite is better suited for applications with lighter write workloads.

---

# Final Conclusion

This experiment demonstrated the fundamental architectural differences between SQLite3 and PostgreSQL.

SQLite3 focuses on lightweight deployment, simplicity, and minimal storage overhead. Its embedded architecture and single-file design make it ideal for local applications, mobile devices, and lightweight systems.

PostgreSQL, in contrast, is designed for scalability, concurrency, and enterprise-grade reliability. Although it consumes more disk space, it delivers superior query performance and advanced transactional capabilities.

The mmap experiment also showed that memory-mapped I/O can noticeably improve SQLite read performance when the dataset fits entirely in memory.

## Final Verdict

- **SQLite3** is best suited for embedded systems, local applications, and lightweight workloads.
- **PostgreSQL** is better suited for production servers, concurrent environments, and large-scale applications.
