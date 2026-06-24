# Advanced DBMS Lab Assignment

## Name
SANAA ARA

## Role Number
24BCS10304

---

# SQLite3 Exploration

## Installation

SQLite3 was already available on macOS.

```bash
sqlite3 --version
```

Output:

```bash
3.51.0
```

---

# Database Creation

Created a sample database named `sample.db`.

```bash
sqlite3 sample.db
```

---

# Table Creation

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

---

# Data Insertion

Inserted 100000 records into the users table.

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

Verification:

```sql
SELECT COUNT(*) FROM users;
```

Output:

```bash
100000
```

---

# File Size Observation

```bash
ls -lh
```

Output:

```bash
-rw-r--r--  1 sanaaara  staff   1.9M sample.db
```

Observation:
- SQLite stores the complete database inside a single `.db` file.
- The database size increased after inserting large data.

---

# PRAGMA Analysis

## Page Size

```sql
PRAGMA page_size;
```

Output:

```bash
4096
```

---

## Page Count

```sql
PRAGMA page_count;
```

Output:

```bash
490
```

---

## mmap_size

```sql
PRAGMA mmap_size;
```

Initial Output:

```bash
0
```

Enabled mmap:

```sql
PRAGMA mmap_size = 268435456;
```

---

# Query Execution Timing

## Without mmap

```sql
PRAGMA mmap_size = 0;
.timer on
SELECT * FROM users;
```

Output:

```bash
Run Time: real 0.121 user 0.035000 sys 0.051143
```

---

## With mmap

```sql
PRAGMA mmap_size = 268435456;
SELECT * FROM users;
```

Output:

```bash
Run Time: real 0.117 user 0.034084 sys 0.048207
```

Observation:
- mmap slightly improved the execution time for read operations.

---

# SQLite Process Observation

```bash
ps aux | grep sqlite
```

Observation:
- SQLite runs as a lightweight standalone process.
- Minimal background activity was observed.

---

# PostgreSQL Setup

## Installation

Installed PostgreSQL using Homebrew.

```bash
brew install postgresql
```

---

# PostgreSQL Version

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

# Table Creation

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INT
);
```

---

# Data Insertion

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

# PostgreSQL Storage Analysis

## Block Size

```sql
SHOW block_size;
```

Output:

```bash
8192
```

---

## Page Count

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

# Query Performance

```sql
\timing
SELECT * FROM users;
```

Output:

```bash
Time: 23.249 ms
```

Observation:
- PostgreSQL handled large query execution efficiently.

---

# PostgreSQL Process Observation

```bash
ps aux | grep postgres
```

Observation:
- PostgreSQL runs multiple background services such as:
  - checkpointer
  - autovacuum launcher
  - walwriter
  - background writer
- Indicates a client-server architecture.

---

# Comparison Between SQLite3 and PostgreSQL

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | Embedded Database | Client-Server Database |
| Page Size | 4096 bytes | 8192 bytes |
| Page Count | 490 | 636 |
| Query Time | 0.121 sec | 23.249 ms |
| mmap Support | Available | Not directly controlled |
| Storage | Single File | Multiple Internal Files |
| Background Processes | Minimal | Multiple System Processes |
| Best Use Case | Small Applications | Large Multi-user Systems |

---

# Conclusion

- SQLite3 is lightweight and easy to configure.
- PostgreSQL provides better scalability and advanced database management.
- mmap improved SQLite read performance slightly.
- PostgreSQL showed efficient performance for handling large datasets.