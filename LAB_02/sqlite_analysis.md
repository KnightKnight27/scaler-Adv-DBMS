# Lab 02 - SQLite vs PostgreSQL Analysis

## Objective

The objective of this lab was to explore SQLite internals including:

- Page Size
- Page Count
- MMAP
- Query Timing
- SQLite vs PostgreSQL comparison

---

# SQLite Database Setup

## Create Database

```bash
sqlite3 sample.db
```

---

## Create Table

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER,
    city TEXT
);
```

---

## Insert Large Dataset

```sql
WITH RECURSIVE cnt(x) AS (
    SELECT 1
    UNION ALL
    SELECT x+1 FROM cnt
    LIMIT 100000
)
INSERT INTO users(name, age, city)
SELECT
    'User' || x,
    20 + (x % 30),
    'City' || (x % 100)
FROM cnt;
```

---

# File Information

## File Size

```bash
ls -lh
```

Output:

```text
-rw-r--r--  sample.db   5.8M
```

---

# SQLite Internals

## Page Size

```sql
PRAGMA page_size;
```

Output:

```text
4096
```

SQLite stores data in fixed-size pages of 4096 bytes.

---

## Page Count

```sql
PRAGMA page_count;
```

Output:

```text
1450
```

Total database size is approximately:

```text
page_size × page_count
4096 × 1450 ≈ 5.9 MB
```

---

# MMAP Analysis

## Check mmap size

```sql
PRAGMA mmap_size;
```

---

## Disable mmap

```sql
PRAGMA mmap_size = 0;
```

---

## Enable mmap

```sql
PRAGMA mmap_size = 268435456;
```

This enables 256 MB memory mapping.

---

# Query Benchmarking

## Query Used

```sql
SELECT * FROM users;
```

---

## WITHOUT mmap

```sql
PRAGMA mmap_size = 0;
```

Timing:

```text
Run Time: real 0.120 user 0.090 sys 0.020
```

---

## WITH mmap

```sql
PRAGMA mmap_size = 268435456;
```

Timing:

```text
Run Time: real 0.075 user 0.060 sys 0.010
```

---

# PostgreSQL Setup

## Create Database

```sql
CREATE DATABASE lab02;
```

---

## Create Table

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INT,
    city TEXT
);
```

---

## Insert Dataset

```sql
INSERT INTO users(name, age, city)
SELECT
    'User' || i,
    20 + (i % 30),
    'City' || (i % 100)
FROM generate_series(1,100000) i;
```

---

# PostgreSQL Query Timing

```sql
SELECT * FROM users;
```

Timing:

```text
Time: 95 ms
```

---

# SQLite vs PostgreSQL Comparison

| Feature | SQLite | PostgreSQL |
|---|---|---|
| Architecture | Embedded | Client-Server |
| Setup Complexity | Simple | Medium |
| Storage | Single File | Multiple Files |
| mmap Support | Direct | Internal |
| Concurrency | Limited | Strong |
| Best Use Case | Small Applications | Large Systems |
| Query Speed | Fast for small workloads | Better scalability |

---

# Key Learnings

- SQLite stores data in pages.
- MMAP reduces data copy overhead.
- Query performance improved slightly with mmap enabled.
- PostgreSQL provides better concurrency and scalability.
- SQLite is lightweight and easy to use.
- PostgreSQL is more suitable for production-scale systems.

---

# Conclusion

This lab helped in understanding:
- SQLite internals
- Database page management
- Memory mapping
- Query benchmarking
- Differences between embedded and client-server databases