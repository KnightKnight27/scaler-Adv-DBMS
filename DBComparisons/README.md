# Database Storage & Performance Report: SQLite3 vs PostgreSQL

## 1. SQLite3 Exploration

### Commands Used

```bash
# Physical file check
ls -lh users.db

# Internal configuration (inside sqlite3)
PRAGMA page_size;
PRAGMA page_count;

# Performance testing
.timer on
PRAGMA mmap_size = 0; -- Disable mmap
SELECT COUNT(*) FROM users;

PRAGMA mmap_size = 268435456; -- Enable mmap (256MB)
SELECT COUNT(*) FROM users;

# Process check (in a separate terminal)
ps aux | grep sqlite

```

### Observations

* **File Size (`ls -lh`):** ~360 KB
* **Page Size:** 4096 bytes
* **Page Count:** 90
* **Query Performance:**
* **Without mmap:** 0.000118s (user time)
* **With mmap:** 0.000144s (user time)


---

## 2. PostgreSQL (PSQL) Exploration

### Commands Used

```sql
-- Check Page (Block) Size
SHOW block_size;

-- Check Page Count and Total Size
SELECT 
    pg_size_pretty(pg_relation_size('users')) AS total_size,
    (pg_relation_size('users') / current_setting('block_size')::int) AS page_count;

-- Performance testing
\timing on
SELECT COUNT(*) FROM users;

```

### Observations

* **Page Size (Block Size):** 8192 bytes
* **Page Count:** 84
* **Total Table Size:** 672 kB
* **Query Performance:** 5.156 ms

---

## 3. Comparison Analysis

| Metric | SQLite3 | PostgreSQL |
| --- | --- | --- |
| **Page Size** | 4096 bytes (4KB) | 8192 bytes (8KB) |
| **Page Count** | 90 | 84 |
| **Execution Time** | ~0.118 ms | 5.156 ms |
| **Architecture** | In-process, serverless. | Multi-process, Client-Server. |

### Analysis of mmap Impact

In SQLite, `mmap` (Memory Mapping) is an explicit configuration. For this specific experiment, the performance difference was negligible because the database file was significantly smaller than the available system RAM. The overhead of the OS setting up the memory map for a ~360KB file actually resulted in a slightly higher execution time compared to a standard cached read.

### Analysis of SQLite vs PostgreSQL

1. **Page Size:** PostgreSQL uses a larger default page size (8KB) compared to SQLite (4KB). Larger pages are generally more efficient for server-side workloads involving large rows and sequential scans, while SQLite’s 4KB pages match standard disk sector sizes for efficiency in smaller environments.
2. **Performance:** SQLite’s count was faster in this test primarily because it lacks the networking and process-intercommunication (IPC) overhead that PostgreSQL requires.
3. **Storage Efficiency:** Even though PostgreSQL used fewer pages (84 vs 90), its total storage footprint was larger (672KB vs 360KB). This is due to PostgreSQL's **MVCC (Multi-Version Concurrency Control)**, which adds row headers to every record to support multiple simultaneous users—a feature SQLite prioritizes less in favor of simplicity.