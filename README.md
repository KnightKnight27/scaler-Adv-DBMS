# SQLite3 V/S PostgreSQL Comparison


# Objective

The objective of this lab is to explore and compare SQLite3 and PostgreSQL with respect to:

- Database storage structure
- Page size and page count
- Query execution performance
- Memory-mapped I/O (mmap) in SQLite
- Process architecture
- Overall performance comparison

---

# Part 1: SQLite3 Exploration

## Installation

SQLite3 was installed on Ubuntu (WSL) using:

```bash
sudo apt update
sudo apt install sqlite3 -y
```

Verification:

```bash
sqlite3 --version
```

Output:

```text
SQLite version 3.37.2
```

---

## Creating Sample Database

Database Creation:

```bash
sqlite3 test.db
```

Table Creation:

```sql
CREATE TABLE users(
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
);
```

Data Insertion:

```sql
INSERT INTO users (name, email) VALUES
('Alice', 'alice@example.com'),
('Bob', 'bob@example.com'),
('Charlie', 'charlie@example.com');
```

Verify Data:

```sql
SELECT * FROM users;
```

Output:

```text
1|Alice|alice@example.com
2|Bob|bob@example.com
3|Charlie|charlie@example.com
```

---

## Database File Size

Command:

```bash
ls -lh test.db
```

Observation:

- SQLite stores the entire database in a single file.
- File size remains small for small datasets.

---

## Page Information

Open SQLite:

```bash
sqlite3 test.db
```

Check Page Size:

```sql
PRAGMA page_size;
```

Output:

```text
4096
```

Check Page Count:

```sql
PRAGMA page_count;
```

Output:

```text
Depends on database contents.
```

Calculate Database Size:

```sql
SELECT page_size * page_count;
```

Observation:

- Default page size is 4096 bytes (4 KB).
- Database size is determined by:
  
  Database Size = Page Size × Page Count

---

## Memory-Mapped I/O (mmap)

Check Current mmap Size:

```sql
PRAGMA mmap_size;
```

Set mmap Size:

```sql
PRAGMA mmap_size = 1000000;
```

Verify:

```sql
PRAGMA mmap_size;
```

Observation:

- mmap allows SQLite to access database pages directly through memory mapping.
- For very small databases, performance improvement is negligible.
- For large databases, mmap can improve read performance.

---

## Query Execution Time

Without mmap:

```bash
time sqlite3 test.db "SELECT * FROM users;"
```

With mmap enabled:

```sql
PRAGMA mmap_size = 1000000;
```

```bash
time sqlite3 test.db "SELECT * FROM users;"
```

Observation:

- Small datasets show almost no noticeable difference.
- mmap is beneficial for larger databases.

---

## SQLite Process Information

Command:

```bash
ps aux | grep sqlite
```

Observation:

- SQLite operates as a lightweight single-process database.
- No dedicated server process is required.

---

# Part 2: PostgreSQL Exploration

## Installation

Install PostgreSQL:

```bash
sudo apt update
sudo apt install postgresql postgresql-contrib -y
```

Start PostgreSQL Service:

```bash
sudo service postgresql start
```

Connect to PostgreSQL:

```bash
sudo -i -u postgres
psql
```

---

## Database Creation

Create Database:

```sql
CREATE DATABASE testdb;
```

Connect:

```sql
\c testdb
```

---

## Creating Sample Table

```sql
CREATE TABLE users(
    id SERIAL PRIMARY KEY,
    name TEXT,
    email TEXT
);
```

Insert Data:

```sql
INSERT INTO users(name,email) VALUES
('Alice','alice@example.com'),
('Bob','bob@example.com'),
('Charlie','charlie@example.com');
```

Verify Data:

```sql
SELECT * FROM users;
```

Output:

```text
 id |  name   |       email
----+---------+------------------
  1 | Alice   | alice@example.com
  2 | Bob     | bob@example.com
  3 | Charlie | charlie@example.com
```

---

## Table Size

Command:

```sql
SELECT pg_size_pretty(pg_total_relation_size('users'));
```

Observation:

- PostgreSQL stores table data across multiple files.
- Storage overhead is higher than SQLite due to advanced features.

---

## Page Size

Command:

```sql
SHOW block_size;
```

Output:

```text
8192
```

Observation:

- PostgreSQL page size is fixed at 8192 bytes (8 KB).

---

## Query Execution Time

Enable Timing:

```sql
\timing
```

Run Query:

```sql
SELECT * FROM users;
```

Observation:

- Execution time is displayed after query execution.
- PostgreSQL scales better for larger datasets and concurrent users.

---

## PostgreSQL Process Information

Command:

```bash
ps aux | grep postgres
```

Observation:

Multiple PostgreSQL processes are visible:

- postmaster
- background writer
- checkpointer
- WAL writer
- autovacuum launcher

This demonstrates PostgreSQL's client-server architecture.

---

# SQLite3 vs PostgreSQL Comparison

| Feature | SQLite3 | PostgreSQL |
|----------|----------|-----------|
| Architecture | Serverless | Client-Server |
| Storage | Single File | Multiple Files |
| Installation | Very Simple | Moderate |
| Default Page Size | 4096 Bytes | 8192 Bytes |
| Page Count | Dynamic | Dynamic |
| Memory Mapping | Supported (mmap) | Internal Buffer Management |
| Query Performance | Excellent for Small Databases | Better for Large Databases |
| Concurrency | Limited | High |
| Server Process | Not Required | Required |
| Scalability | Low to Medium | High |
| Best Use Case | Embedded Applications | Enterprise Applications |

---

# Observations

## SQLite3

Advantages:

- Lightweight
- Easy setup
- No server required
- Single database file
- Fast for small datasets

Limitations:

- Limited concurrency
- Not ideal for large-scale applications

---

## PostgreSQL

Advantages:

- Highly scalable
- Supports multiple users
- Advanced indexing and optimization
- Better concurrency management

Limitations:

- More resource intensive
- Requires server process

---

# Analysis of mmap Impact

SQLite supports memory-mapped I/O through:

```sql
PRAGMA mmap_size;
```

Benefits:

- Reduces system calls
- Faster read operations
- Better performance for large databases

Observation from experiment:

- Minimal effect on very small databases.
- Potentially significant improvement for larger datasets.

PostgreSQL does not expose mmap settings directly because it manages memory through:

- Shared Buffers
- Buffer Cache
- Internal Memory Management

---

# Conclusion

SQLite3 and PostgreSQL are designed for different use cases.

- SQLite is ideal for lightweight, embedded, and single-user applications.
- PostgreSQL is suitable for enterprise systems requiring scalability, reliability, and concurrent access.

From the experiments:

- SQLite uses a default page size of 4096 bytes and benefits from mmap-based optimization.
- PostgreSQL uses a fixed page size of 8192 bytes and relies on sophisticated internal memory management.
- SQLite offers simplicity and speed for small workloads.
- PostgreSQL provides better scalability and performance for larger and multi-user environments.

Therefore, SQLite is best suited for small standalone applications, whereas PostgreSQL is better suited for production-grade database systems.
