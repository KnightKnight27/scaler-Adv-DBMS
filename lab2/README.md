# Lab 2 — SQLite Internal Storage Analysis & SQLite3 vs PostgreSQL Comparison

## Student Information

**Name:** Jatin Chulet
**Roll Number:** 24BCS10213
**Course:** Advanced DBMS / Storage Systems

---

# Objective

The objective of this laboratory is to explore database storage systems through two practical experiments:

1. **SQLite Database Internal Structure Inspection** using hexadecimal analysis.
2. **SQLite3 vs PostgreSQL Comparative Study** focusing on architecture, storage, memory management, and performance characteristics.

These experiments provide insights into how databases are physically stored and how different database systems manage data internally.

---

# Experiment 1: SQLite Database Internal Structure Inspection

## Introduction

SQLite stores an entire database inside a single file. Understanding the internal layout of this file helps reveal how relational data, schemas, pages, and metadata are represented at the byte level.

A hexadecimal dump of a SQLite database file was generated and analyzed using the `xxd` utility.

---

## Tools Used

* SQLite3 Command Line Interface
* xxd Hex Dump Utility
* Windows PowerShell
* Git Bash
* SQLite Database Engine

---

## Database Description

Database File:

```text
jatin_projects.db
```

### Table Schema

```sql
CREATE TABLE projects(
    project_id INTEGER PRIMARY KEY,
    project_name TEXT,
    team_lead TEXT,
    technology TEXT,
    status TEXT
);
```

### Sample Records

| Project ID | Project Name     | Team Lead     | Technology | Status      |
| ---------- | ---------------- | ------------- | ---------- | ----------- |
| 1          | Storage Engine   | Jatin Chulet  | C++        | Completed   |
| 2          | Image Captioning | Kartik Bhatia | PyTorch    | In Progress |
| 3          | MentorConnect    | Kshitij Singh | React      | Completed   |

---

## Generating the Hex Dump

```bash
xxd -g 1 jatin_projects.db > jatin_dump.txt
```

This command generates a byte-level hexadecimal representation of the database file.

---

## SQLite File Header Analysis

First 16 bytes:

```text
53 51 4C 69 74 65 20 66 6F 72 6D 61 74 20 33 00
```

ASCII representation:

```text
SQLite format 3
```

This confirms that the file is a valid SQLite database.

---

## Page Size Information

Header bytes:

```text
10 00
```

Converted value:

```text
0x1000 = 4096 bytes
```

Therefore, the database uses a page size of **4096 bytes**.

---

## Schema Storage

SQLite stores table definitions inside the internal table:

```text
sqlite_master
```

The schema text can be observed directly inside the database pages through hexadecimal inspection.

---

## Record Storage Observation

SQLite stores records using:

* B-Tree pages
* Variable-length integers (Varints)
* Record headers
* Serialized column values

This compact representation minimizes storage overhead.

---

## Key Observations

1. Database begins with the signature **SQLite format 3**.
2. Data is organized into fixed-size pages.
3. Table definitions are stored inside the database itself.
4. Records are stored in B-Tree structures.
5. Metadata and user data are visible within the hexadecimal dump.

---

# Experiment 2: SQLite3 vs PostgreSQL Comparison

## Introduction

SQLite and PostgreSQL are widely used database systems with significantly different architectures.

This experiment compares them in terms of:

* Storage architecture
* Memory management
* Query performance
* Page organization
* Process behavior
* Scalability

---

## Files Included

```text
comparison.sh
README.md
```

### Running the Experiment

```bash
chmod +x comparison.sh
./comparison.sh
```

---

## SQLite3 Exploration

### Database Creation

```sql
CREATE TABLE students(
    id INTEGER PRIMARY KEY,
    name TEXT
);

INSERT INTO students(name)
VALUES ('Jatin'), ('Kartik'), ('Kshitij');
```

### Storage Observation

SQLite stores the entire database inside a single file:

```text
jatin_lab.db
```

Advantages:

* Lightweight
* Portable
* Minimal setup
* Embedded execution

---

### Page Information

Commands:

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
```

Observations:

* Default page size is typically 4096 bytes.
* Data is stored in fixed-size pages.
* Small databases require only a few pages.

---

### Memory-Mapped I/O

Commands:

```sql
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
```

Observations:

* SQLite supports memory-mapped access.
* Reduces system call overhead.
* Can improve read performance.

---

### Query Timing

Commands:

```sql
.timer on

SELECT * FROM students;

PRAGMA mmap_size = 268435456;

SELECT * FROM students;
```

Observations:

* Very low execution overhead.
* Fast performance for small datasets.
* Benefits from memory mapping.

---

### Process Behavior

Command:

```bash
ps aux | grep sqlite
```

Observation:

* SQLite runs inside the application process.
* No dedicated server process exists.

---

## PostgreSQL Exploration

### Database Creation

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

### Database Size

Command:

```sql
SELECT pg_size_pretty(
    pg_database_size('jatin_db')
);
```

Observations:

PostgreSQL consumes additional storage for:

* System catalogs
* Transaction logs (WAL)
* Metadata
* Recovery information

---

### Page Information

Command:

```sql
SHOW block_size;
```

Observation:

Default page size:

```text
8192 bytes (8 KB)
```

Approximate page count:

```sql
SELECT
pg_relation_size('students') /
current_setting('block_size')::int
AS approx_page_count;
```

---

### Query Timing

Commands:

```sql
\timing on

SELECT * FROM students;
SELECT * FROM students;
SELECT * FROM students;
```

Observations:

* Repeated queries become faster.
* Shared buffers improve performance.
* Effective caching mechanisms exist.

---

### Shared Memory Information

Commands:

```sql
SHOW shared_buffers;
SHOW effective_cache_size;
```

Observations:

* PostgreSQL uses dedicated shared memory.
* Automatic memory optimization.
* Designed for concurrent workloads.

---

### Query Plan Analysis

Command:

```sql
EXPLAIN ANALYZE
SELECT * FROM students;
```

Observations:

* Small tables typically use sequential scans.
* Query plans provide execution details.
* Useful for optimization and tuning.

---

### Process Observation

Command:

```bash
ps aux | grep postgres
```

Background Processes:

* Checkpointer
* WAL Writer
* Background Writer
* Autovacuum Launcher
* Logger

These processes support recovery, concurrency, and performance.

---

# SQLite3 vs PostgreSQL Comparison

| Feature              | SQLite3               | PostgreSQL              |
| -------------------- | --------------------- | ----------------------- |
| Architecture         | Embedded              | Client-Server           |
| Storage              | Single Database File  | Multiple Internal Files |
| Default Page Size    | 4096 Bytes            | 8192 Bytes              |
| Memory Optimization  | mmap_size             | shared_buffers          |
| Server Process       | Not Required          | Required                |
| Background Processes | None                  | Multiple                |
| Resource Usage       | Low                   | Higher                  |
| Setup Complexity     | Simple                | Moderate                |
| Scalability          | Limited               | High                    |
| Best Use Case        | Embedded Applications | Enterprise Systems      |

---

# Key Learnings

* SQLite stores the complete database in a single portable file.
* SQLite uses B-Tree structures internally for data organization.
* Hexadecimal analysis provides visibility into low-level database storage.
* PostgreSQL employs a client-server architecture with advanced memory management.
* PostgreSQL supports concurrency, caching, transaction logging, and recovery mechanisms.
* Both systems use page-based storage but differ significantly in scalability and complexity.

---

# Conclusion

This laboratory successfully explored both the internal storage structure of SQLite databases and the architectural differences between SQLite3 and PostgreSQL.

The first experiment demonstrated how SQLite stores schemas, metadata, and records within a binary database file using page-based B-Tree structures. The second experiment highlighted the trade-offs between SQLite's lightweight embedded design and PostgreSQL's feature-rich client-server architecture.

Together, these experiments provided a deeper understanding of modern database storage systems, memory management techniques, and indexing mechanisms used in real-world applications.
