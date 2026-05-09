# Database Internals Lab Report

### SQLite3 vs PostgreSQL: Storage, mmap, Processes, and Query Timing

Name: Ananth Adatta  
Roll Number: YOUR_ROLE_NUMBER  
Sample database: users table (`sample.db`)

---

## 1. Environment Setup

| Component | Details |
|---|---|
| Operating System | macOS |
| SQLite version | 3.51.0 |
| SQLite database created | `sample.db` |
| PostgreSQL version | PostgreSQL 18.3 |
| PostgreSQL database used | `labdb` |
| PostgreSQL server | `localhost` |
| PostgreSQL installation method | Homebrew |

---

## 2. SQLite3 Exploration

### 2.1 File Size and Page Metadata

Commands used:

```bash
ls -lh
```

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
```

Observed values:

| Metric | Observed Value |
|---|---:|
| SQLite database file | `sample.db` |
| File size | 8.0 KB |
| Page size | 4096 bytes |
| Page count | 2 pages |
| Default `mmap_size` | 0 |

Verification:

```text
2 pages * 4096 bytes = 8192 bytes
```

Observation:

The SQLite database size exactly matched the calculated page size multiplied by page count.

---

### 2.2 Database Creation and Data Insertion

Commands used:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

```sql
INSERT INTO users(name, age)
VALUES
('Alice', 21),
('Bob', 22),
('Charlie', 23);
```

Verification query:

```sql
SELECT * FROM users;
```

Output:

```text
1|Alice|21
2|Bob|22
3|Charlie|23
```

---

### 2.3 Query Timing Without mmap

Command used:

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

Observed output:

```text
0.011 total
```

Observation:

SQLite query execution completed very quickly because the dataset was extremely small.

---

### 2.4 Query Timing With mmap

Command used:

```sql
PRAGMA mmap_size = 268435456;
```

Verification:

```sql
PRAGMA mmap_size;
```

Output:

```text
268435456
```

Query timing command:

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

Observed output:

```text
0.018 total
```

---

### 2.5 mmap Impact Summary

| Query | Without mmap | With mmap |
|---|---:|---:|
| `SELECT * FROM users;` | 0.011 s | 0.018 s |

Observation:

- mmap did not improve performance for this very small dataset.
- Query execution time slightly increased after enabling mmap.
- mmap benefits are generally more visible for large databases and heavy read workloads.

---

### 2.6 SQLite Process Inspection

Command used:

```bash
ps aux | grep sqlite
```

Observed result:

| Observation | Result |
|---|---|
| Long-running SQLite server process | Not found |
| Reason | SQLite is embedded and runs inside the calling application |

Observation:

SQLite does not require a dedicated server process.

---

## 3. PostgreSQL Exploration

### 3.1 PostgreSQL Installation and Service Check

Commands used:

```bash
brew install postgresql
```

```bash
brew services start postgresql
```

Verification:

```bash
brew services list
```

Observed output:

| Service | Status |
|---|---|
| `postgresql@18` | started |

Observation:

PostgreSQL service started successfully using Homebrew.

---

### 3.2 PostgreSQL Database and Table Creation

Commands used:

```sql
CREATE DATABASE labdb;
```

```sql
\c labdb
```

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INT
);
```

```sql
INSERT INTO users(name, age)
VALUES
('Alice', 21),
('Bob', 22),
('Charlie', 23);
```

Verification query:

```sql
SELECT * FROM users;
```

Output:

```text
1 | Alice   | 21
2 | Bob     | 22
3 | Charlie | 23
```

---

### 3.3 PostgreSQL Storage Metadata

Commands used:

```sql
SHOW block_size;
```

```sql
SELECT pg_relation_size('users');
```

```sql
SELECT pg_relation_size('users') / 8192;
```

Observed values:

| Metric | Observed Value |
|---|---:|
| PostgreSQL block size | 8192 bytes |
| Relation size | 8192 bytes |
| Estimated page count | 1 page |

Observation:

PostgreSQL used an 8 KB block size and the users table occupied approximately one page.

---

### 3.4 PostgreSQL Query Timing

Commands used:

```sql
\timing
```

```sql
SELECT * FROM users;
```

Observed output:

```text
Time: 0.559 ms
```

Observation:

PostgreSQL query execution was very fast for the small dataset.

---

### 3.5 PostgreSQL Process Inspection

Command used:

```bash
ps aux | grep postgres
```

Observed PostgreSQL processes included:

| PostgreSQL Process |
|---|
| checkpointer |
| background writer |
| walwriter |
| autovacuum launcher |
| logical replication launcher |
| io workers |

Observation:

- PostgreSQL runs multiple background processes.
- PostgreSQL follows a client-server architecture unlike SQLite.

---

## 4. SQLite3 vs PostgreSQL Comparison

### 4.1 Storage and Page Comparison

| Metric | SQLite3 | PostgreSQL |
|---|---:|---:|
| Storage Style | Single database file | Internal relation files |
| Database File Size | 8192 bytes | Relation-based storage |
| Page/Block Size | 4096 bytes | 8192 bytes |
| Page Count | 2 | 1 |
| mmap Support | Direct PRAGMA support | Internally managed |
| Architecture | Embedded | Client-Server |

---

### 4.2 Query Performance Comparison

| Query | SQLite Without mmap | SQLite With mmap | PostgreSQL |
|---|---:|---:|---:|
| `SELECT * FROM users;` | 0.011 s | 0.018 s | 0.559 ms |

Observation:

- PostgreSQL query timing was lower in milliseconds.
- SQLite remained extremely lightweight and simple.
- mmap did not improve SQLite performance due to the tiny dataset size.

---

### 4.3 mmap and Memory Behavior

| Area | SQLite3 | PostgreSQL |
|---|---|---|
| mmap control | `PRAGMA mmap_size` | No direct SQL PRAGMA |
| mmap tested value | 268435456 bytes | Not applicable |
| Default value | 0 | Internal memory management |
| Observed impact | Slightly slower on small dataset | Uses server-managed caching |

Observation:

SQLite exposes mmap configuration directly, whereas PostgreSQL internally manages memory and caching behavior.

---

### 4.4 Architecture Comparison

| Aspect | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | Embedded database | Server-based DBMS |
| Server Process | No | Yes |
| Setup Complexity | Very low | Moderate |
| Best Use Case | Lightweight/local applications | Multi-user scalable systems |
| Concurrency Support | Limited | Strong |

---

## 5. Commands Reference

### SQLite3 Commands

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
SELECT * FROM users;
```

```bash
ls -lh
time sqlite3 sample.db "SELECT * FROM users;"
ps aux | grep sqlite
```

---

### PostgreSQL Commands

```sql
CREATE DATABASE labdb;
\c labdb

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INT
);

INSERT INTO users(name, age)
VALUES
('Alice', 21),
('Bob', 22),
('Charlie', 23);

SHOW block_size;
SELECT pg_relation_size('users');
SELECT pg_relation_size('users') / 8192;

\timing
SELECT * FROM users;
```

```bash
brew services start postgresql
brew services list
ps aux | grep postgres
```

---

## 6. Conclusions

1. SQLite used a 4096-byte page size and the database occupied 2 pages with a total file size of 8192 bytes.

2. SQLite mmap was initially disabled (`mmap_size = 0`). After enabling mmap with `268435456` bytes, query execution time slightly increased from 0.011 s to 0.018 s because the dataset was very small.

3. PostgreSQL used an 8192-byte block size and managed storage internally using relation files rather than a single visible database file.

4. PostgreSQL query execution time for `SELECT * FROM users;` was approximately 0.559 ms.

5. SQLite did not run any dedicated background server process, while PostgreSQL showed multiple active server processes such as background writer, WAL writer, autovacuum launcher, and checkpointer.

6. SQLite is lightweight and ideal for embedded/local applications, whereas PostgreSQL is better suited for scalable multi-user systems.

7. mmap improvements are workload-dependent and become more beneficial with larger databases and read-intensive operations.
