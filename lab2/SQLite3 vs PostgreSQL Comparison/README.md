# SQLite3 vs PostgreSQL Comparison

## Student Information

**Name:** Jatin Chulet

**Roll Number:** 24BCS10213

**Batch:** A

**Course:** ADV-DBMS

---

## Objective

The objective of this experiment is to compare SQLite3 and PostgreSQL in terms of:

* Storage architecture
* Page size and page count
* Query execution performance
* Memory optimization techniques
* Process behavior
* Overall database design

---

## Files Included

* `comparison.sh` – Bash script containing all experiments
* `README.md` – Documentation and observations

### Running the Experiment

```bash
chmod +x comparison.sh
./comparison.sh
```

---

# SQLite3 Exploration

## Database Creation

```sql
CREATE TABLE students(
    id INTEGER PRIMARY KEY,
    name TEXT
);

INSERT INTO students(name)
VALUES ('Jatin'), ('Kartik'), ('Kshitij');
```

### Observation

SQLite stores the entire database inside a single file named:

```text
jatin_lab.db
```

---

## File Size Observation

Command Used:

```bash
ls -lh jatin_lab.db
```

### Observation

* SQLite stores all data in a single `.db` file.
* The database occupies very little disk space.
* Suitable for lightweight applications and embedded systems.

---

## Page Information

Commands Used:

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
```

### Observation

* SQLite organizes data into fixed-size pages.
* Default page size is typically 4096 bytes.
* Small databases require only a few pages.

---

## Memory-Mapped I/O Experiment

Commands Used:

```sql
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
```

### Observation

* SQLite supports memory-mapped I/O.
* Database pages can be accessed directly through virtual memory.
* This can improve performance by reducing system call overhead.

---

## Query Timing Experiment

Commands Used:

```sql
.timer on

SELECT * FROM students;

PRAGMA mmap_size = 268435456;

SELECT * FROM students;
```

### Observation

* Query execution is extremely fast for small datasets.
* Performance may improve slightly when memory mapping is enabled.
* SQLite has very low overhead because it runs inside the application process.

---

## Process Observation

Command Used:

```bash
ps aux | grep sqlite
```

### Observation

* SQLite does not run as a dedicated server.
* No background database processes exist.
* The database engine is embedded directly into the application.

---

# PostgreSQL Exploration

## Database Creation

```sql
CREATE DATABASE jatin_db;

CREATE TABLE students(
    id SERIAL PRIMARY KEY,
    name TEXT
);

INSERT INTO students(name)
VALUES ('Jatin'), ('Kartik'), ('Kshitij');
```

---

## Database Size

Command Used:

```sql
SELECT pg_size_pretty(pg_database_size('jatin_db'));
```

### Observation

* PostgreSQL databases consume significantly more storage than SQLite.
* Additional storage is used for:

    * System catalogs
    * Transaction logs (WAL)
    * Metadata
    * Internal management structures

---

## Page Information

Command Used:

```sql
SHOW block_size;
```

### Observation

* PostgreSQL uses pages (blocks) for storage.
* Default block size is usually 8192 bytes (8 KB).

Approximate page count:

```sql
SELECT
    pg_relation_size('students') /
    current_setting('block_size')::int
    AS approx_page_count;
```

---

## Query Timing

Commands Used:

```sql
\timing on

SELECT * FROM students;
SELECT * FROM students;
SELECT * FROM students;
```

### Observation

* Query execution becomes faster after repeated runs.
* PostgreSQL benefits from caching and shared memory buffers.

---

## Shared Memory Information

Commands Used:

```sql
SHOW shared_buffers;
SHOW effective_cache_size;
```

### Observation

* PostgreSQL manages memory using shared buffers.
* It relies heavily on caching for improved performance.
* Memory management is automatic and optimized for concurrent workloads.

---

## Query Plan Analysis

Command Used:

```sql
EXPLAIN ANALYZE
SELECT * FROM students;
```

### Observation

* PostgreSQL typically performs a sequential scan for very small tables.
* Execution time is extremely low.
* Query plans help understand how PostgreSQL accesses data.

---

## Process Observation

Command Used:

```bash
ps aux | grep postgres
```

### Observation

PostgreSQL runs multiple background processes such as:

* Checkpointer
* WAL Writer
* Background Writer
* Autovacuum Launcher
* Logger

These processes support concurrency, recovery, and performance optimization.

---

# Comparison Between SQLite3 and PostgreSQL

| Feature              | SQLite3                       | PostgreSQL                           |
| -------------------- | ----------------------------- | ------------------------------------ |
| Architecture         | Embedded                      | Client-Server                        |
| Storage              | Single `.db` file             | Multiple internal files              |
| Default Page Size    | 4096 Bytes                    | 8192 Bytes                           |
| Memory Optimization  | `mmap_size`                   | `shared_buffers`                     |
| Server Process       | Not Required                  | Required                             |
| Background Processes | None                          | Multiple                             |
| Resource Usage       | Low                           | Higher                               |
| Setup Complexity     | Simple                        | Moderate                             |
| Scalability          | Limited                       | High                                 |
| Best Use Case        | Embedded / Local Applications | Enterprise / Multi-user Applications |

---

# Conclusion

SQLite3 is a lightweight embedded database that stores all information in a single file and requires no dedicated server process. It is simple, efficient, and well suited for local applications and small-scale systems.

PostgreSQL follows a client-server architecture and provides advanced features such as shared memory management, background workers, transaction logging, and support for concurrent users. Although it requires more resources, it offers superior scalability and reliability for large applications.

This experiment successfully demonstrated the architectural and performance differences between SQLite3 and PostgreSQL.
