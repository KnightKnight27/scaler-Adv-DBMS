# Advanced DBMS Lab Assignment  
## SQLite3 vs PostgreSQL: Architectural and Performance Comparison

### Name
**Akarsh Garg**

### Roll Number
**24BCS10181**

---

# Objective

The objective of this lab assignment is to perform a detailed comparative analysis between **SQLite3** and **PostgreSQL** by studying their:

- Architecture
- Storage mechanism
- Page/block management
- Query execution performance
- Memory-mapped I/O behavior (mmap)
- Process models
- Concurrency support
- Resource utilization

The experiment aims to understand how embedded databases differ from enterprise-grade relational database systems in terms of design, scalability, and performance.

---

# Introduction

Database Management Systems (DBMS) are responsible for storing, managing, and retrieving data efficiently. Different databases are optimized for different workloads and system requirements.

This assignment compares two widely used databases:

- **SQLite3** → Lightweight embedded database
- **PostgreSQL** → Advanced client-server relational database

Although both are relational databases supporting SQL, their internal architecture and target use cases are completely different.

---

# Part 1 — SQLite3 Exploration

## About SQLite3

SQLite3 is a:
- Serverless
- Embedded
- File-based relational database

Unlike traditional databases, SQLite does not require a dedicated server process. The entire database is stored inside a single `.db` file.

SQLite is widely used in:
- Mobile applications
- Embedded systems
- Desktop software
- Browser storage engines
- IoT devices

Official Website: https://www.sqlite.org

---

## SQLite3 Installation Verification

### Command

```bash
sqlite3 --version
```

### Output

```bash
3.51.0
```

---

## Database Creation

```bash
sqlite3 test.db
```

Observation:
- SQLite automatically creates the database file if it does not exist.
- Entire database exists inside one physical file.

---

## Table Creation

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

---

## Data Insertion

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

---

## Verification

```sql
SELECT COUNT(*) FROM users;
```

Output:

```sql
100000
```

---

## PRAGMA Analysis

### Page Size

```sql
PRAGMA page_size;
```

Output:

```text
4096
```

### Page Count

```sql
PRAGMA page_count;
```

Output:

```text
490 – 2242
```

### mmap Analysis

#### Default mmap Size

```sql
PRAGMA mmap_size;
```

Output:

```text
0
```

#### Enable mmap

```sql
PRAGMA mmap_size = 268435456;
```

Enables:
- 256 MB memory mapping

---

## Query Performance Testing

### Without mmap

```sql
PRAGMA mmap_size = 0;
.timer on
SELECT * FROM users;
```

Output:

```text
Run Time: real 0.121 sec
```

### With mmap

```sql
PRAGMA mmap_size = 268435456;
SELECT * FROM users;
```

Output:

```text
Run Time: real 0.117 sec
```

Observation:
- mmap slightly improved performance.
- Since database fit in OS page cache, improvements remained minimal.

---

## SQLite Process Observation

```bash
ps aux | grep sqlite
```

Observation:
- No dedicated SQLite server process exists.
- SQLite operates as an embedded library.

---

## SQLite Advantages

- Lightweight
- Portable
- Zero configuration
- Minimal memory usage
- Fast local reads

---

## SQLite Limitations

- Single writer limitation
- Limited concurrency
- Database-level locking

---

# Part 2 — PostgreSQL Exploration

## About PostgreSQL

PostgreSQL is:
- Open-source
- Object-relational
- Client-server database system

Designed for:
- High concurrency
- Scalability
- Reliability
- Enterprise workloads

Official Website: https://www.postgresql.org

---

## PostgreSQL Version

```bash
psql --version
```

Output:

```bash
psql (PostgreSQL) 18.3
```

---

## Starting PostgreSQL Service

### Linux

```bash
sudo systemctl start postgresql
```

### macOS

```bash
/opt/homebrew/opt/postgresql@18/bin/postgres -D ... -p 55432
```

---

## Database Creation

```sql
CREATE DATABASE advdbms;
```

Connect:

```sql
\c advdbms
```

---

## Table Creation

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INT
);
```

---

## Data Insertion

```sql
INSERT INTO users(name, age)
SELECT
    'User' || generate_series,
    20 + (generate_series % 10)
FROM generate_series(1, 100000);
```

---

## Block Size Analysis

```sql
SHOW block_size;
```

Output:

```text
8192
```

Observation:
- PostgreSQL uses 8 KB blocks.

---

## Page Count

```sql
SELECT relpages
FROM pg_class
WHERE relname = 'users';
```

Output:

```text
636 – 935
```

---

## Database Size

```sql
SELECT pg_size_pretty(pg_relation_size('users'));
```

Output:

```text
6.4 MB – 7.5 MB
```

---

## Query Performance

```sql
EXPLAIN ANALYZE
SELECT * FROM users;
```

Output:

```text
Execution Time: 6.5 ms – 37 ms
```

Observation:
- PostgreSQL consistently outperformed SQLite for large scans.
- Advanced query planner optimized execution efficiently.

---

## PostgreSQL Process Architecture

```bash
ps aux | grep postgres
```

Observed Processes:
- postmaster
- checkpointer
- walwriter
- background writer
- autovacuum launcher
- logical replication launcher
- I/O workers

Observation:
- PostgreSQL uses a robust multi-process client-server architecture.

---

# Comparative Analysis

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | Embedded Database | Client-Server Database |
| Storage Model | Single `.db` file | Multiple files + WAL |
| Page Size | 4096 bytes | 8192 bytes |
| Query Performance | Good for local workloads | Better for concurrent workloads |
| mmap Support | Direct support | Shared buffers |
| Concurrency | Limited | Full MVCC |
| Background Processes | None | Multiple |
| Setup Complexity | Minimal | Moderate |
| Scalability | Limited | High |
| Best Use Case | Embedded apps | Enterprise systems |

---

# Key Findings

1. SQLite is lightweight and easy to manage.
2. PostgreSQL provides superior scalability and concurrency.
3. SQLite exposes low-level storage details clearly.
4. PostgreSQL abstracts storage management internally.
5. mmap only slightly improved SQLite performance.
6. PostgreSQL consistently delivered faster query execution.
7. SQLite uses no background services.
8. PostgreSQL uses multiple background worker processes.

---

# Real-World Use Cases

## SQLite
- Mobile apps
- Embedded systems
- Browser databases
- Desktop applications
- IoT devices

## PostgreSQL
- Enterprise systems
- Banking systems
- SaaS applications
- Analytics systems
- Multi-user platforms

---

# Final Conclusion

SQLite3 and PostgreSQL are both powerful relational databases, but they are optimized for different environments.

SQLite3 focuses on:
- Simplicity
- Portability
- Lightweight deployment

It is ideal for:
- Low concurrency systems
- Embedded applications
- Local storage

PostgreSQL focuses on:
- Scalability
- Reliability
- Concurrent transaction handling
- Enterprise-grade optimization

Experimental observations showed:
- PostgreSQL achieved better query performance and concurrency handling.
- SQLite demonstrated exceptional simplicity and portability.
- mmap produced only minor improvements because the database already fit into OS page cache.

Therefore:
- **SQLite is best for lightweight embedded workloads**
- **PostgreSQL is best for scalable multi-user production systems**

---

# References

- SQLite Documentation: https://www.sqlite.org/docs.html
- PostgreSQL Documentation: https://www.postgresql.org/docs/
- SQLite mmap Documentation: https://www.sqlite.org/mmap.html
