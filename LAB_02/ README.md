# Lab 02 - SQLite vs PostgreSQL Analysis

## Objective

The objective of this lab is to explore and compare SQLite and PostgreSQL internals by analyzing:

- Database page size
- Page count
- MMAP behavior
- Query execution timing
- Storage architecture
- Query performance

This lab also focuses on understanding how databases internally manage memory and storage.

---

# Technologies Used

- SQLite3
- PostgreSQL (PSQL)
- SQL
- Linux/macOS Terminal
- VS Code
- Git & GitHub

---

# Project Structure

```bash
Lab_02/
│
├── README.md
├── sqlite_analysis.md
├── queries.sql
├── sample.db
└── screenshots/
```

---

# SQLite Setup

## Open SQLite Database

```bash
sqlite3 sample.db
```

---

# Create Table

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER,
    city TEXT
);
```

---

# Insert Large Dataset

```sql
WITH RECURSIVE cnt(x) AS (
    SELECT 1
    UNION ALL
    SELECT x + 1 FROM cnt
    LIMIT 100000
)
INSERT INTO users(name, age, city)
SELECT
    'User' || x,
    20 + (x % 30),
    'City' || (x % 100)
FROM cnt;
```

This creates a dataset with 100,000 rows for benchmarking.

---

# Database File Information

## Check File Size

```bash
ls -lh
```

This displays:

- database file size
- storage usage
- file permissions

---

# SQLite Internal Analysis

## Check Page Size

```sql
PRAGMA page_size;
```

Example Output:

```text
4096
```

SQLite stores data in fixed-size pages.

---

## Check Page Count

```sql
PRAGMA page_count;
```

Example Output:

```text
1450
```

Approximate database size:

```text
page_size × page_count
```

---

# MMAP Analysis

## Check Current mmap Size

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

This enables:

```text
256 MB memory mapped I/O
```

---

# Query Benchmarking

## Enable Timer

```sql
.timer on
```

---

# Query Without mmap

```sql
PRAGMA mmap_size = 0;

SELECT * FROM users;
```

---

# Query With mmap

```sql
PRAGMA mmap_size = 268435456;

SELECT * FROM users;
```

---

# PostgreSQL Setup

## Install PostgreSQL

### macOS

```bash
brew install postgresql
```

---

# Start PostgreSQL

```bash
brew services start postgresql
```

---

# Open PostgreSQL

```bash
psql postgres
```

---

# Create Database

```sql
CREATE DATABASE lab02;
```

---

# Connect Database

```sql
\c lab02
```

---

# Create Table

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INT,
    city TEXT
);
```

---

# Insert Dataset

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

Enable timing:

```sql
\timing
```

Run query:

```sql
SELECT * FROM users;
```

---

# SQLite vs PostgreSQL Comparison

| Feature | SQLite | PostgreSQL |
|---|---|---|
| Architecture | Embedded | Client-Server |
| Storage | Single File | Multi-file |
| Setup | Simple | Moderate |
| Concurrency | Limited | Strong |
| mmap Support | Direct | Internal |
| Best Use Case | Small Applications | Large Systems |
| Scalability | Limited | High |

---

# Concepts Covered

- Database Pages
- Page Size
- Page Count
- Memory Mapping
- mmap
- Query Benchmarking
- SQLite Internals
- PostgreSQL Architecture
- Storage Analysis
- Query Performance

---

# Learning Outcomes

Through this lab the following concepts were understood:

- SQLite page-based storage
- Memory mapped I/O
- Query performance analysis
- Differences between embedded and server databases
- Database benchmarking
- Internal database architecture

---

# Sample Queries Used

- `SELECT * FROM users;`
- `SELECT COUNT(*) FROM users;`
- `SELECT * FROM users WHERE age = 25;`
- `SELECT * FROM users ORDER BY age;`

---

# Author

Prince Kumar

Scaler School of Technology  
BITS Pilani

---

# Conclusion

This lab provided practical understanding of:

- database internals
- storage organization
- mmap optimization
- query timing
- embedded vs client-server databases

SQLite proved lightweight and simple, while PostgreSQL demonstrated stronger scalability and concurrency capabilities.