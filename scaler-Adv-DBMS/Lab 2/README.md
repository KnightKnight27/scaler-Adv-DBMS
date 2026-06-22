# Database Internals: Storage and Performance Evaluation

**Name:** Nandani Kumari
**Roll no.:** 24bcs10317
---

## Part 1: SQLite3 Analysis

### Database Setup
A database named `test_db.sqlite` was generated using a custom Python script. It contains an `employees` table with 150,000 records.

**File Size Check Command:**
```powershell
ls -lh "Lab 2/test_db.sqlite"
```
**Observation:**
- **Total File Size:** 7.92 MB

### Internal Storage Parameters (PRAGMA)
The following internal metrics were retrieved using PRAGMA queries:

| Parameter | Observed Value | SQL Command |
| :--- | :--- | :--- |
| **Page Size** | 4096 bytes | `PRAGMA page_size;` |
| **Total Page Count** | 2027 | `PRAGMA page_count;` |
| **mmap Size Limit** | 0 (Disabled by default) | `PRAGMA mmap_size;` |

### Query Execution & Memory-Mapped I/O (mmap)
We benchmarked a full table scan query (`SELECT * FROM employees;`) under two different memory mapping configurations:

| Scenario | Average Execution Time |
| :--- | :--- |
| **Standard I/O (mmap disabled)** | 0.1436 seconds |
| **Memory Mapped I/O (128 MB)** | 0.1356 seconds |

**Analysis:**
Activating `mmap_size` provided a noticeable speedup for data retrieval. By mapping the database file directly into the process's RAM space, SQLite circumvents the overhead of traditional `read()` system calls and double-buffering between kernel and user space.

### Process Tracking
**Command used to track database instances:**
```bash
ps aux | grep sqlite
# Or in Windows PowerShell:
# Get-Process -Name "*sqlite*" -ErrorAction SilentlyContinue
```

---

## Part 2: PostgreSQL Evaluation

### Server Configuration
PostgreSQL operates as an independent background service. The following metrics were collected from a typical local installation.

| Configuration Item | Observed Value | SQL Command |
| :--- | :--- | :--- |
| **Block/Page Size** | 8192 bytes (8KB) | `SHOW block_size;` |
| **Estimated Page Count** | ~1400 (for 150k rows) | `SELECT relpages FROM pg_class WHERE relname='employees';` |

### Query Performance Profile
Unlike SQLite, PostgreSQL relies heavily on its own `shared_buffers` architecture rather than direct OS-level mmap for its primary caching layer.

| Scenario | Average Execution Time |
| :--- | :--- |
| **Full Table Scan (SELECT *)** | ~0.18s |

---

## Part 3: Comparative Summary

The differences between SQLite3 and PostgreSQL in terms of storage architecture and memory management are quite distinct.

| Characteristic | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **System Architecture** | Embedded Library (Serverless) | Dedicated Client-Server Model |
| **Storage Unit Size** | 4 KB (4096 bytes) | 8 KB (8192 bytes) |
| **Storage Density** | More pages required per table | Fewer pages due to larger blocks |
| **Memory Strategy** | Leverages OS Page Cache (mmap) | Uses dedicated `shared_buffers` |
| **Best Use Case** | Local applications, read-heavy workloads | High concurrency, multi-user systems |

### Final Thoughts
1. **Page Architecture:** PostgreSQL defaults to an 8KB block size, which is better suited for server-grade hardware and complex indexing structures. SQLite uses 4KB pages, which aligns perfectly with standard OS memory page sizes.
2. **Buffering Approach:** Enabling `mmap` in SQLite is a lightweight way to bypass the standard I/O stack. PostgreSQL takes a more robust approach, allocating a fixed chunk of system RAM (`shared_buffers`) and managing caching internally, which provides better transactional consistency and performance under concurrent loads.
