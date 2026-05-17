# scaler-Adv-DBMS

# SQLite3 vs PostgreSQL: A Detailed System Comparison

**Name:** Arman Barbhuiya
**Roll Number:** 24BSC10196

## Overview
This report outlines an experimental comparison between two distinct database models: **SQLite3** (a serverless, embedded engine) and **PostgreSQL** (a powerful client-server RDBMS). Using local benchmarks, we analyzed differences in file storage, memory management, query speed, and architectural processes to determine their respective operational strengths.

---

## 1. SQLite3: Embedded Database Evaluation

### Experimental Setup
We initialized a sample database to inspect its default behaviors and architectural footprint. The following commands were executed to analyze the behavior of an SQLite3 database:

```bash
sqlite3 test.db
ls -lh
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
```

| Metric | Observation |
| :--- | :--- |
| **Architecture** | In-process library (Serverless) |
| **Default Page Size** | 4096 bytes (4 KB) |
| **Initial File Size** | ~8 KB |

### Process Inspection
Unlike traditional databases, SQLite does not run as a persistent background daemon. Running `ps aux | grep sqlite` confirmed that no active background processes exist. The engine operates purely as an embedded library that exists only while the calling process is active.

### Memory-Mapping (`mmap_size`) Experiment
We tested the impact of memory-mapping to see if bypassing standard `read()` syscalls would improve performance:
* **Command:** `PRAGMA mmap_size = 268435456;`
* **Result:** System execution time remained effectively static at **0.003s**. 
* **Analysis:** Because the 8KB database file is significantly smaller than the operating system's page cache minimum threshold, the overhead is negligible regardless of whether memory-mapping is enabled.

---

## 2. PostgreSQL: Client-Server RDBMS Evaluation

### Setup & Initialization
Because PostgreSQL operates as a persistent daemon, we initialized the server and created a dedicated role and testing database:

```sql
CREATE ROLE arman WITH LOGIN SUPERUSER PASSWORD '190205';
CREATE DATABASE my_lab_db;
```

### Storage & Page Analysis
* **Block Size:** Executing `SHOW block_size;` confirmed an **8192 byte (8 KB)** default.
* **Page Allocation:** The initial page count returned 0. After executing `VACUUM ANALYZE users;`, the `relpages` metric updated to 1, representing the first allocated 8KB block. This highlights PostgreSQL's requirement for manual (or autovacuum) statistics updates compared to SQLite's automated handling.

### Performance & Connection Overhead
We measured query execution from the terminal to analyze client-server handshake latency:
* **Internal Server Execution Time:** 0.600 ms (measured via `\timing on`)
* **Wall-Clock Time:** 0.060s (Real), 0.017s (User), 0.035s (Sys)
* **Analysis:** The total wall-clock time is roughly 100x slower than the internal execution. This discrepancy demonstrates the significant overhead of establishing a Unix socket connection, authenticating the user role, and initiating a backend worker process before a query can even run.

---

## 3. Comparative Analysis

### Page Size Strategy
* **SQLite3 (4 KB):** Aligns perfectly with standard disk sectors, making it highly optimized for local file I/O and low-power edge devices.
* **PostgreSQL (8 KB):** The larger, compile-time constant size is optimized for server-grade I/O. It allows more records per page, significantly reducing the depth of B-tree indexes when parsing massive datasets.

### Architecture & Concurrency
* **The "Sprinter" (SQLite):** Utilizes direct file I/O with near-instantaneous startup times (~5ms overhead). It is perfect for isolated, local tasks but lacks robust concurrent write capabilities.
* **The "Enterprise Runner" (PostgreSQL):** Relies on a socket-based connection (TCP/Unix). It trades higher initial connection overhead (~60ms) for enterprise-grade concurrency, user roles, and complex transaction management.

### Memory Management
* **SQLite3:** Offers a direct manual toggle (`mmap_size`) for memory mapping, which directly benefits applications once the database size exceeds the OS cache limit.
* **PostgreSQL:** Does not use a direct `mmap` PRAGMA. Instead, it manages memory intrinsically via `shared_buffers` and relies heavily on the operating system's `effective_cache_size` to handle file mapping automatically.
