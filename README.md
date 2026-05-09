# Advanced DBMS - Lab Experiment Report
**Student Name:** Rohan Ranjan  
**Role Number:** 24BCS10428

---

## 1. SQLite3 Exploration

### Setup & Data Generation
I created a sample database `sample.db` with a `users` table containing 100,000 records to observe significant performance metrics.

**Commands Used:**
```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT,
    data BLOB
);

-- Insert 100,000 rows
WITH RECURSIVE cnt(x) AS (
     SELECT 1 UNION ALL SELECT x+1 FROM cnt LIMIT 100000
)
INSERT INTO users (name, email, data)
SELECT 'User' || x, 'user' || x || '@example.com', randomblob(1024) FROM cnt;
```

### Observations
| Metric | Value | Command |
| :--- | :--- | :--- |
| **File Size** | 131 MB | `ls -lh sample.db` |
| **Page Size** | 4096 bytes | `PRAGMA page_size;` |
| **Page Count** | 33,418 | `PRAGMA page_count;` |

### MMAP Experiment
SQLite's `mmap_size` determines the maximum number of bytes that can be mapped into memory.

**Performance Comparison:**
- **Without MMAP (`mmap_size=0`):** ~0.140s
- **With MMAP (`mmap_size=256MB`):** ~0.121s

**Observation:** Memory mapping (MMAP) improves performance by reducing the need for explicit I/O system calls, allowing the OS to manage database pages as part of virtual memory.

---

## 2. PostgreSQL (PSQL) Setup

### Configuration Metrics
PostgreSQL uses a different storage architecture compared to SQLite's single-file system.

**Commands Used:**
```sql
-- Find Page Size
SHOW block_size;

-- Find Page Count for a table
SELECT relpages FROM pg_class WHERE relname = 'users';
```

### Observations (Typical)
| Metric | Value | Note |
| :--- | :--- | :--- |
| **Page Size** | 8192 bytes | Default PostgreSQL block size. |
| **Execution Time** | ~0.080s | Varies based on shared buffer configuration. |

---

## 3. Comparison Analysis

| Feature | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Architecture** | File-based (Serverless) | Client-Server |
| **Default Page Size** | 4096 bytes | 8192 bytes |
| **Memory Mapping** | Explicit `mmap_size` control | Relies on `shared_buffers` and OS Cache |
| **Performance** | Excellent for local reads | Optimized for concurrent workloads |
| **Impact of MMAP** | Significant reduction in I/O overhead | Managed inherently by OS/Buffer manager |

### Conclusion
SQLite is highly efficient for single-user, local applications where the overhead of a server is unnecessary. PostgreSQL, while having a larger default page size and more complex memory management, scales better for high-concurrency environments and complex relational queries.

---

## How to Run
1. **SQLite:** Run `sqlite3 sample.db` and execute the PRAGMA commands.
2. **PostgreSQL:** Ensure the service is running, log in via `psql`, and use the `SHOW` and `SELECT` commands mentioned above.
