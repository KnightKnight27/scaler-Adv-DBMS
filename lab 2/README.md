# Comparative Analysis: SQLite3 and PostgreSQL Storage Engines & Query Performance

**Author:** Abdullah Danish  
**Roll No:** 24BCS10054  
**Date:** May 9, 2026  
**Platform:** Windows x86_64 (PowerShell terminal)  
**Test Dataset:** 100,000 synthetic user records

---

## Environment Preparation

### Software Versions

```powershell
# Confirm SQLite3 availability
sqlite3 --version
# 3.45.1 2024-01-30 16:01:20

# PostgreSQL 17 — deployed via the EnterpriseDB Windows installer (x86-64)
```

### Table Definition Used Across Both Systems

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT,
    email TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

---

## Part A: Investigating SQLite3 Internals

### Storage Footprint

```powershell
Get-Item sample.db | Select-Object Name, Length
```

**Result:**
```
Name       Length
----       ------
sample.db 6111232
```

SQLite consolidates everything — table definitions, row data, indices, and internal metadata — into a **single self-contained file** (`sample.db`, approximately 6.1 MB for our dataset).

---

### Probing Internal Configuration via PRAGMA

#### Inspecting Page Size

```sql
PRAGMA page_size;
```

**Returned:** `4096`

SQLite organizes its storage into fixed-size pages, defaulting to **4096 bytes (4 KB)**. Every disk read and write operation is performed at this granularity.

#### Counting Allocated Pages

```sql
PRAGMA page_count;
```

**Returned:** `1492`

With 1,492 pages at 4 KB each, the calculated total is `1492 × 4096 = 6,111,232 bytes ≈ 6.1 MB`, which aligns precisely with the observed file size on disk.

---

### Experimenting with Memory-Mapped I/O (mmap)

#### Baseline Configuration (mmap inactive)

```sql
PRAGMA mmap_size=0;
```

In its default state, SQLite relies exclusively on conventional `read()` system calls routed through the kernel's page cache for all data access.

#### Enabling mmap with a 256 MB Window

```sql
PRAGMA mmap_size=268435456;
```

Configuring `mmap_size` to 256 MB directs SQLite to map up to that amount of the database file directly into the process's virtual address space via the `mmap()` system call. This eliminates the intermediate copy step that `read()` requires, allowing the application to access file data through ordinary memory pointers. Since our entire database is only 6.1 MB, the full file gets mapped into virtual memory.

---

### Benchmarking Queries — SQLite3

Timing measured with PowerShell's `Measure-Command` wrapper:

```powershell
# Baseline — no memory mapping
Measure-Command { sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" | Out-Null }

# With memory mapping enabled
Measure-Command { sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" | Out-Null }
```

**Measured Timings:**

| Workload | Standard I/O | Memory-Mapped (256 MB) |
|---|---|---|
| `SELECT * FROM users` (100K rows) | 1343.15 ms | 1139.50 ms |

**Interpretation:**
- Memory mapping delivered roughly a **15% latency reduction** (from 1343 ms down to 1139 ms) on a full sequential scan.
- The speedup stems from avoiding the `read()` → kernel buffer → user buffer copy chain. With mmap, the application accesses data through direct virtual memory pointers, and the OS page cache transparently handles fault-in from disk.

---

### Process Architecture Observation

```powershell
Get-Process | Where-Object {$_.Name -match "sqlite"}
```

SQLite operates as an **embedded library linked directly into the host process**. There is no daemon, no background service, and no client-server communication overhead. The sqlite3 CLI process terminates as soon as the query finishes executing.

---

## Part B: PostgreSQL Configuration and Testing

### Service Lifecycle

PostgreSQL operates as a persistent background service on Windows (registered as `postgresql-x64-17`), accepting connections from client processes via TCP or local sockets.

### Block Size Inspection

```sql
SHOW block_size;
```

**Returned:** `8192`

PostgreSQL's default block size is **8192 bytes (8 KB)** — exactly double what SQLite uses. This larger granularity is optimized for server-class I/O patterns where sequential throughput matters more than memory efficiency.

### Relation Size Analysis

```sql
SELECT relname, relpages, pg_size_pretty(pg_total_relation_size(oid)) AS total_size
FROM pg_class WHERE relname = 'users';

SELECT pg_size_pretty(pg_database_size('postgres'));
```

**Approximate Results for 100K Records:**

| Relation | Page Count | Aggregate Size |
|---|---|---|
| users | ~1,200 | ~10 MB |

PostgreSQL's larger footprint (10 MB vs. SQLite's 6.1 MB for identical data) is attributable to per-tuple MVCC headers (`xmin`, `xmax`, `ctid`, etc.), WAL segment overhead, and system catalog metadata. This is the cost of supporting full multi-version concurrency control.

---

### Benchmarking Queries — PostgreSQL

```sql
\timing on
SELECT * FROM users;
```

**Measured Timing:**

| Workload | Execution Time |
|---|---|
| `SELECT * FROM users` (100K rows) | ~850 ms |

PostgreSQL's performance advantage on large sequential scans comes from its 8 KB block size (fewer I/O operations per scan), a sophisticated shared buffer pool, and the ability to leverage parallel query workers when advantageous.

---

## Part C: Head-to-Head Comparison

### C.1 — Page/Block Size Characteristics

| Attribute | SQLite3 | PostgreSQL |
|---|---|---|
| Default page/block size | **4,096 bytes** (4 KB) | **8,192 bytes** (8 KB) |
| User-configurable? | Yes (at database creation time) | Yes (at compile time only) |
| Inspection command | `PRAGMA page_size` | `SHOW block_size` |

**Discussion:**
SQLite's 4 KB default mirrors the typical OS virtual memory page size, which makes it well-suited for resource-constrained embedded environments. PostgreSQL's 8 KB blocks favor larger sequential reads, reducing the per-page overhead for server workloads that process substantial data volumes.

---

### C.2 — Storage Overhead Comparison

| Metric | SQLite3 | PostgreSQL |
|---|---|---|
| Pages used by `users` | **1,492** (at 4 KB each) | **~1,200** (at 8 KB each) |
| Total database size | **6.1 MB** | **~10 MB** |

**Discussion:**
PostgreSQL attaches significantly more per-row metadata (MVCC visibility fields like `xmin`, `xmax`, `cmin`, `cmax`, `ctid`) to support safe concurrent transactions. This overhead is a deliberate engineering trade-off: the extra storage enables row-level isolation and prevents readers from blocking writers.

---

### C.3 — Query Throughput Comparison

| Workload | SQLite3 (standard) | SQLite3 (mmap on) | PostgreSQL |
|---|---|---|---|
| Full scan of 100K rows | 1343 ms | 1139 ms | ~850 ms |

**Discussion:**
PostgreSQL's superior throughput is driven by its multi-process architecture, parallel query execution capability, and a mature cost-based query optimizer. SQLite's single-threaded design limits its throughput ceiling, though enabling memory mapping helps close the gap by eliminating redundant data copies.

---

### C.4 — Deep Dive: mmap Behavior in SQLite3

| Configuration | Latency (100K rows) | I/O Mechanism |
|---|---|---|
| `mmap_size=0` (default) | 1343 ms | Traditional `read()` syscall path |
| `mmap_size=268435456` (256 MB) | 1139 ms | Direct virtual memory access via `mmap()` |

**Mechanics of mmap in SQLite:**
Under the standard path, SQLite issues `read()` syscalls that copy file data first into kernel buffers, then into SQLite's internal page cache in user-space — a double-copy scenario. When `mmap_size` is set to a non-zero value, SQLite maps the database file into its virtual address space, allowing direct pointer-based access to file pages. The operating system's virtual memory subsystem handles page faults and eviction transparently.

**How PostgreSQL Achieves Comparable Optimization:**
PostgreSQL does not expose a user-facing mmap toggle. Instead, it maintains a highly tuned `shared_buffers` pool (typically 25% of system RAM) complemented by OS-level page caching. The buffer manager implements clock-sweep eviction, ring buffer strategies for sequential scans, and background writer processes — all designed for high-concurrency server workloads rather than single-process embedded use.

---

### C.5 — Consolidated Feature Matrix

| Dimension | SQLite3 | PostgreSQL |
|---|---|---|
| Deployment model | Serverless, in-process library | Client-server with persistent daemon |
| Page / block size | 4 KB (default) | 8 KB (default) |
| Storage layout | Single `.db` file | Multi-file cluster directory |
| Memory-mapped I/O | Configurable via `PRAGMA mmap_size` | Not directly user-configurable |
| Query parallelism | None (strictly single-threaded) | Parallel workers per query |
| Concurrency model | Limited (file-level locking) | Full MVCC with row-level visibility |
| Write concurrency | Single writer at a time | Multiple concurrent writers (row-level locks) |
| Disk footprint (same data) | 6.1 MB | ~10 MB (MVCC overhead) |
| Ideal deployment scenario | Embedded apps, local tooling, testing | Production servers, high-concurrency workloads |

---

## Concluding Observations

This lab exposed the fundamental architectural differences between an embedded database engine and a full-featured RDBMS:

1. **Storage Philosophy:** SQLite's elegance lies in its single-file simplicity — 1,492 pages of 4 KB each, totaling 6.1 MB, with zero operational overhead. PostgreSQL accepts greater storage costs (MVCC tuple headers, WAL segments, system catalogs) in exchange for robust multi-user concurrency guarantees.

2. **I/O Optimization Strategies:** Enabling `mmap` in SQLite yielded a measurable 15% improvement by eliminating the user-space copy step. PostgreSQL achieves analogous optimization through its shared buffer architecture, which is purpose-built for concurrent access patterns that a simple `mmap` cannot safely support.

3. **Practical Guidance:** SQLite remains the optimal choice when the workload is single-user, read-heavy, and deployment simplicity is paramount. PostgreSQL is the appropriate selection when the application demands concurrent writes, transactional isolation, and horizontal scalability.

---
*Lab 2 — Abdullah Danish (24BCS10054)*
