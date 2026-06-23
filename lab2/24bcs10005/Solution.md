# Introduction

This lab investigates the internal storage mechanisms and query execution behavior of two widely used relational database systems:

* SQLite
* PostgreSQL

The objective was to compare:

* Disk storage layout
* Page/block organization
* Memory and cache behavior
* Query execution time
* Process architecture
* Resource overhead

The **Chinook** sample database was used as a common dataset for both systems.

---

# Environment Setup

SQLite3 was already available in the Ubuntu environment. PostgreSQL was installed using the Ubuntu package manager and executed as a background database service.

## Installation Commands

```bash
# Update package index
sudo apt update

# Install SQLite3
sudo apt install -y sqlite3

# Install PostgreSQL
sudo apt install -y postgresql postgresql-contrib

# Start PostgreSQL service
sudo service postgresql start
```

### Installed Versions

| Software   | Version |
| ---------- | ------- |
| SQLite3    | 3.45.1  |
| PostgreSQL | 16.13   |

---

# Dataset Preparation

The **Chinook** database simulates a digital media store and contains realistic relational data such as:

* Artists
* Albums
* Tracks
* Customers
* Invoices
* Employees

## Download Commands

```bash
# SQLite database file
wget https://github.com/lerocha/chinook-database/raw/master/ChinookDatabase/DataSources/Chinook_Sqlite.sqlite

# PostgreSQL SQL dump
wget https://raw.githubusercontent.com/lerocha/chinook-database/master/ChinookDatabase/DataSources/Chinook_PostgreSql.sql
```

## PostgreSQL Import

```bash
sudo -u postgres createdb chinook

sudo cp ~/Chinook_PostgreSql.sql /tmp/

sudo -u postgres psql chinook -f /tmp/Chinook_PostgreSql.sql
```

---

# Part 1 — SQLite3 Analysis

## Database File Size

SQLite stores the complete database inside a single portable file.

### Command

```bash
ls -lh ~/Chinook_Sqlite.sqlite
```

### Output

```bash
-rw-r--r-- 1 vimal vimal 984K May 7 05:32 /home/vimal/Chinook_Sqlite.sqlite
```

| Property           | Value  |
| ------------------ | ------ |
| Database File Size | 984 KB |

### Observation

SQLite follows a lightweight embedded architecture where:

* Tables
* Indexes
* Metadata
* Transaction journals

are managed through one flat database file.

This design makes SQLite highly portable and simple to deploy.

---

## Page-Level Storage Information

SQLite organizes data internally using fixed-size pages.

### Commands

```bash
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA page_size;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA page_count;"
```

### Output

```bash
4096
246
```

| Parameter   | Value      |
| ----------- | ---------- |
| Page Size   | 4096 bytes |
| Total Pages | 246        |

### Calculation

4096 \times 246 = 1,007,616

Total storage occupied is approximately **1 MB**, closely matching the actual database file size.

### Observation

SQLite’s default page size is **4 KB**, which aligns with the standard Linux memory page size. This improves compatibility with the operating system's page cache and minimizes wasted memory during page loading.

---

## Additional PRAGMA Information

### Commands

```bash
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA journal_mode;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA cache_size;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA integrity_check;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA freelist_count;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA synchronous;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA encoding;"
```

### Output

```bash
delete
-2000
ok
0
2
UTF-8
```

| PRAGMA            | Value    | Explanation                  |
| ----------------- | -------- | ---------------------------- |
| `journal_mode`    | delete   | Uses rollback journal files  |
| `cache_size`      | -2000    | Around 8 MB cache allocation |
| `integrity_check` | ok       | Database is not corrupted    |
| `freelist_count`  | 0        | No unused pages exist        |
| `synchronous`     | 2 (FULL) | Ensures durable disk writes  |
| `encoding`        | UTF-8    | Text encoding format         |

### Observation

The database file was fully compacted with no unused pages. SQLite prioritizes durability using FULL synchronous mode, meaning writes are flushed safely to disk before commit completion.

---

## mmap Experiment

SQLite supports memory-mapped file access.

### Commands

```bash
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA mmap_size;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA mmap_size=30000000;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA mmap_size;"
```

### Output

```bash
0
30000000
30000000
```

| State   | Value    |
| ------- | -------- |
| Default | Disabled |
| Enabled | 30 MB    |

### Observation

By default, memory mapping is disabled.

After enabling mmap:

* SQLite can directly map the database file into virtual memory
* Fewer read system calls are needed
* Kernel page faults handle loading transparently

This optimization is more beneficial on larger datasets.

---

## Query Execution Timing

### Without mmap

```bash
time sqlite3 ~/Chinook_Sqlite.sqlite "SELECT * FROM Invoice;"
```

### Output

```bash
real    0m0.004s
```

---

### With mmap Enabled

```bash
time sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA mmap_size=30000000; SELECT * FROM Invoice;"
```

### Output

```bash
real    0m0.003s
```

| Mode         | Execution Time |
| ------------ | -------------- |
| Normal       | 0.004s         |
| mmap Enabled | 0.003s         |

### Observation

Only a small improvement was observed because the database is already very small and easily cached in memory.

For larger databases, mmap can significantly reduce I/O overhead.

---

## SQLite Built-in Timing

### Command

```bash
sqlite3 ~/Chinook_Sqlite.sqlite <<'EOF'
.timer on
SELECT * FROM Invoice;
EOF
```

### Output

```bash
Run Time: real 0.002 user 0.002109 sys 0.000000
```

### Observation

Most execution time occurred in user-space processing rather than system-level disk access, indicating that the data was already cached.

---

## Process Architecture

### Command

```bash
ps aux | grep sqlite
```

### Output

```bash
vimal 1696 0.0 0.0 4092 1920 pts/0 S+ 05:34 0:00 grep --color=auto sqlite
```

### Observation

No dedicated SQLite server process exists.

SQLite is an embedded database engine that runs directly inside the application process itself.

Advantages include:

* Zero server setup
* No IPC overhead
* Minimal memory usage

However, SQLite supports only one writer at a time.

---

# Part 2 — PostgreSQL Analysis

## PostgreSQL Service Information

### Commands

```bash
sudo apt install -y postgresql postgresql-contrib
sudo service postgresql start
```

| Property          | Value                         |
| ----------------- | ----------------------------- |
| Version           | 16.13                         |
| Default Encoding  | UTF8                          |
| Cluster Directory | `/var/lib/postgresql/16/main` |

---

## Block Size and Relation Pages

### Commands

```bash
sudo -u postgres psql chinook -c "SHOW block_size;"

sudo -u postgres psql chinook -c "SELECT relpages FROM pg_class WHERE relname = 'invoice';"
```

### Output

```bash
8192

6
```

| Property            | Value      |
| ------------------- | ---------- |
| Block Size          | 8192 bytes |
| Invoice Table Pages | 6          |

### Observation

PostgreSQL uses **8 KB fixed-size blocks**.

Unlike SQLite, PostgreSQL block size is determined during compilation and generally remains constant for the entire cluster.

Larger blocks improve efficiency for large sequential scans and server-based workloads.

---

## Relation Size and Storage Files

### Commands

```bash
sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_relation_size('invoice'));"

sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_indexes_size('invoice'));"

sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_total_relation_size('invoice'));"

sudo -u postgres psql chinook -c "SELECT pg_relation_filepath('invoice');"
```

### Output

```bash
48 kB
48 kB
120 kB
base/16384/16410
```

| Metric              | Value            |
| ------------------- | ---------------- |
| Heap Data           | 48 kB            |
| Indexes             | 48 kB            |
| Total Relation Size | 120 kB           |
| File Path           | base/16384/16410 |

### Observation

PostgreSQL stores:

* Tables
* Indexes
* Visibility maps
* Free-space maps

as separate physical files.

This modular structure supports:

* MVCC
* Parallel access
* Crash recovery
* Advanced indexing

---

## Query Timing

### Command

```bash
time sudo -u postgres psql chinook -c "SELECT * FROM invoice;" > /dev/null
```

### Output

```bash
real    0m0.030s
```

### Observation

PostgreSQL appeared slower than SQLite for this simple query because additional work occurs before execution:

* Client-server communication
* Authentication
* Query planning
* Process management

Actual execution time inside the engine was much smaller.

---

## Query Planner Analysis

## Sequential Scan

### Command

```bash
sudo -u postgres psql chinook -c "EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM invoice;"
```

### Output

```bash
Seq Scan on invoice
Buffers: shared hit=6
Execution Time: 0.061 ms
```

### Observation

The planner selected a sequential scan because the table was small.

`shared hit=6` indicates:

* All required pages were already available in shared memory
* No physical disk reads occurred

---

## GROUP BY Aggregation

### Command

```bash
sudo -u postgres psql chinook -c "
EXPLAIN ANALYZE
SELECT billing_country,
COUNT(*),
ROUND(AVG(total)::numeric, 2)
FROM invoice
GROUP BY billing_country
ORDER BY COUNT(*) DESC;"
```

### Observation

PostgreSQL used:

* **HashAggregate** for grouping
* **Quicksort** for sorting final output

The planner optimized memory usage automatically while maintaining low execution time.

---

## Memory Configuration

### Commands

```bash
sudo -u postgres psql chinook -c "SHOW shared_buffers;"
sudo -u postgres psql chinook -c "SHOW work_mem;"
sudo -u postgres psql chinook -c "SHOW maintenance_work_mem;"
sudo -u postgres psql chinook -c "SHOW effective_cache_size;"
```

### Output

```bash
128MB
4MB
64MB
4GB
```

| Parameter              | Value  | Purpose                            |
| ---------------------- | ------ | ---------------------------------- |
| `shared_buffers`       | 128 MB | Shared page cache                  |
| `work_mem`             | 4 MB   | Memory for sorting/hashing         |
| `maintenance_work_mem` | 64 MB  | Used during maintenance operations |
| `effective_cache_size` | 4 GB   | Planner estimate for OS cache      |

### Observation

PostgreSQL relies heavily on centralized shared memory pools to support concurrent users and query optimization.

---

## Total Database Size

### Command

```bash
sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_database_size('chinook'));"
```

### Output

```bash
10023 kB
```

| Property            | Value  |
| ------------------- | ------ |
| Total Database Size | ~10 MB |

### Observation

PostgreSQL required significantly more storage than SQLite because of:

* System catalogs
* WAL files
* Index structures
* Visibility maps
* MVCC metadata

This additional overhead provides stronger concurrency and reliability guarantees.

---

## PostgreSQL Process Architecture

### Command

```bash
ps aux | grep postgres
```

### Observation

Several PostgreSQL background services were running:

| Process                      | Responsibility            |
| ---------------------------- | ------------------------- |
| postmaster                   | Main server controller    |
| checkpointer                 | Flushes dirty pages       |
| background writer            | Optimizes write activity  |
| walwriter                    | Maintains WAL durability  |
| autovacuum launcher          | Handles VACUUM operations |
| logical replication launcher | Supports replication      |

### Observation

Unlike SQLite’s embedded model, PostgreSQL operates as a complete database server ecosystem designed for:

* High concurrency
* Transaction isolation
* Fault tolerance
* Multi-user workloads

---

# Comparative Summary

| Feature              | SQLite3             | PostgreSQL                  |
| -------------------- | ------------------- | --------------------------- |
| Architecture         | Embedded            | Client-server               |
| Storage Model        | Single file         | Multiple relation files     |
| Page Size            | 4 KB                | 8 KB                        |
| Database Size        | 984 KB              | ~10 MB                      |
| Query Timing         | Faster startup      | More planning overhead      |
| Concurrency          | Single writer       | Multiple concurrent writers |
| Query Planner        | Simpler             | Cost-based optimizer        |
| Background Processes | None                | Multiple daemons            |
| WAL Support          | Optional journaling | Native WAL                  |
| Best Use Case        | Lightweight apps    | Enterprise systems          |

---

# Final Analysis

The experiments clearly demonstrate that architecture fundamentally influences database behavior.

SQLite prioritizes:

* Simplicity
* Portability
* Low overhead
* Embedded deployment

It performs extremely well for:

* Mobile applications
* Local tools
* Small projects
* Offline software

PostgreSQL prioritizes:

* Scalability
* Reliability
* Concurrency
* Advanced optimization

It is more suitable for:

* Multi-user systems
* Large-scale web applications
* Enterprise backends
* Analytical workloads

Although PostgreSQL consumed more disk space and introduced additional overhead, it provides powerful infrastructure for concurrent transactions, crash recovery, and sophisticated query optimization.

SQLite remains highly efficient for lightweight workloads where simplicity and minimal resource consumption are more important than concurrency.

---

# Recommended Usage

| Scenario                         | Recommended Database |
| -------------------------------- | -------------------- |
| Embedded/mobile apps             | SQLite3              |
| Offline desktop applications     | SQLite3              |
| Rapid prototyping                | SQLite3              |
| Enterprise applications          | PostgreSQL           |
| High concurrency workloads       | PostgreSQL           |
| Large analytical queries         | PostgreSQL           |
| Production-scale backend systems | PostgreSQL           |

---

# Commands Reference

## SQLite3 Commands

```bash
ls -lh ~/Chinook_Sqlite.sqlite

sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA page_size;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA page_count;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA mmap_size;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA mmap_size=30000000;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA journal_mode;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA cache_size;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA integrity_check;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA freelist_count;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA synchronous;"
sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA encoding;"

time sqlite3 ~/Chinook_Sqlite.sqlite "SELECT * FROM Invoice;"

time sqlite3 ~/Chinook_Sqlite.sqlite "PRAGMA mmap_size=30000000; SELECT * FROM Invoice;"

ps aux | grep sqlite
```

---

## PostgreSQL Commands

```bash
sudo -u postgres psql chinook -c "SHOW block_size;"

sudo -u postgres psql chinook -c "SELECT relpages FROM pg_class WHERE relname = 'invoice';"

sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_relation_size('invoice'));"

sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_indexes_size('invoice'));"

sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_total_relation_size('invoice'));"

sudo -u postgres psql chinook -c "SELECT pg_relation_filepath('invoice');"

sudo -u postgres psql chinook -c "SELECT pg_size_pretty(pg_database_size('chinook'));"

sudo -u postgres psql chinook -c "SHOW shared_buffers;"

sudo -u postgres psql chinook -c "SHOW work_mem;"

sudo -u postgres psql chinook -c "SHOW maintenance_work_mem;"

sudo -u postgres psql chinook -c "SHOW effective_cache_size;"

sudo -u postgres psql chinook -c "EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM invoice;"

time sudo -u postgres psql chinook -c "SELECT * FROM invoice;" > /dev/null

ps aux | grep postgres
```
