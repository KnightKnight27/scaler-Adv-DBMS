# Advanced DBMS Laboratory Assignment

## Student Name
CHHAVI AHLAWAT

## Role Number
24BCS10201

---

# Experiment 1: SQLite3 Exploration

## Objective

To study SQLite3 storage structure, page allocation, mmap behavior, and query performance.

---

# SQLite Installation

SQLite3 was pre-installed on the macOS system.

Verification command:

```bash
sqlite3 --version
```

Observed Version:

```bash
3.51.0
```

---

# Creating the Database

A sample database named `sample.db` was created.

```bash
sqlite3 sample.db
```

---

# Creating users Table

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

---

# Populating Large Dataset

100000 records were inserted for performance analysis.

```sql
WITH RECURSIVE cnt(x) AS (
    SELECT 1
    UNION ALL
    SELECT x+1 FROM cnt WHERE x < 100000
)
INSERT INTO users(name, age)
SELECT 'User' || x, 20 + (x % 10)
FROM cnt;
```

Verification query:

```sql
SELECT COUNT(*) FROM users;
```

Result:

```bash
100000
```

---

# File Size Analysis

```bash
ls -lh
```

Observed Output:

```bash
-rw-r--r--  1 sanaaara  staff   1.9M sample.db
```

Observation:
- SQLite maintains the entire database inside a compact local file.

---

# SQLite PRAGMA Commands

## Checking Page Size

```sql
PRAGMA page_size;
```

Output:

```bash
4096
```

---

## Checking Page Count

```sql
PRAGMA page_count;
```

Output:

```bash
490
```

---

## mmap Experiment

Initial mmap value:

```sql
PRAGMA mmap_size;
```

Output:

```bash
0
```

Updated mmap value:

```sql
PRAGMA mmap_size = 268435456;
```

---

# Query Timing Comparison

## Query Without mmap

```sql
PRAGMA mmap_size = 0;
.timer on
SELECT * FROM users;
```

Observed Runtime:

```bash
Run Time: real 0.121 user 0.035000 sys 0.051143
```

---

## Query With mmap Enabled

```sql
PRAGMA mmap_size = 268435456;
SELECT * FROM users;
```

Observed Runtime:

```bash
Run Time: real 0.117 user 0.034084 sys 0.048207
```

Observation:
- Enabling mmap produced a small improvement in query execution speed.

---

# SQLite Process Monitoring

```bash
ps aux | grep sqlite
```

Observation:
- SQLite used very few system resources and operated as a simple process.

---

# Experiment 2: PostgreSQL Setup and Analysis

## PostgreSQL Installation

Installed using Homebrew package manager.

```bash
brew install postgresql
```

---

# PostgreSQL Version Check

```bash
psql --version
```

Output:

```bash
psql (PostgreSQL) 18.3
```

---

# Database Creation

```sql
CREATE DATABASE advdbms;
```

Connected using:

```sql
\c advdbms
```

---

# Table Setup

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INT
);
```

---

# Inserting Test Data

```sql
INSERT INTO users(name, age)
SELECT
    'User' || generate_series,
    20 + (generate_series % 10)
FROM generate_series(1, 100000);
```

Verification:

```sql
SELECT COUNT(*) FROM users;
```

Output:

```bash
100000
```

---

# PostgreSQL Page Information

## Block Size

```sql
SHOW block_size;
```

Output:

```bash
8192
```

---

## relpages Observation

```sql
SELECT relpages
FROM pg_class
WHERE relname = 'users';
```

Output:

```bash
636
```

---

# PostgreSQL Query Timing

```sql
\timing
SELECT * FROM users;
```

Observed Output:

```bash
Time: 23.249 ms
```

Observation:
- PostgreSQL demonstrated strong query performance with large datasets.

---

# PostgreSQL Process Analysis

```bash
ps aux | grep postgres
```

Observation:
- Multiple PostgreSQL background processes were active.
- Included:
  - autovacuum launcher
  - walwriter
  - checkpointer
  - background writer

This highlights PostgreSQL’s server-oriented architecture.

---

# Comparative Study

| Parameter | SQLite3 | PostgreSQL |
|---|---|---|
| Database Type | Embedded | Server-based |
| Storage Model | Single File | Managed Storage System |
| Page Size | 4096 Bytes | 8192 Bytes |
| Page Count | 490 | 636 |
| Query Runtime | 0.121 sec | 23.249 ms |
| mmap Feature | Supported | Internal Memory Management |
| Complexity | Simple | Advanced |
| Scalability | Limited | High |

---

# Final Observation

SQLite3 is suitable for lightweight and embedded applications due to its simplicity and low overhead. PostgreSQL is more suitable for enterprise-scale systems requiring concurrency, reliability, and advanced database features.