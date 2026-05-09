# Database Storage Engine Exploration Lab

## Name
Shifa

## Role Number
10354

---

# 1. SQLite3 Exploration

## Installation

```bash
sudo apt install sqlite3
```

## Creating Sample Database

```bash
sqlite3 sample.db
```

Inside SQLite:

```sql
CREATE TABLE users(
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);

INSERT INTO users(name, age)
VALUES
('Alice', 21),
('Bob', 22),
('Charlie', 23);
```

Exit:

```sql
.exit
```

---

## Checking File Size

```bash
ls -lh
```

### Observation

- SQLite stores the entire database inside a single `.db` file.
- File size increases as more rows are inserted.

---

## Finding Page Size

```bash
sqlite3 sample.db
```

```sql
PRAGMA page_size;
```

### Output

```text
4096
```

### Observation

- SQLite default page size is 4096 bytes (4KB).

---

## Finding Page Count

```sql
PRAGMA page_count;
```

### Observation

- Page count increases with inserted data.
- Total DB size:

```text
Database Size = page_size × page_count
```

---

## Experimenting with mmap_size

### Checking Current mmap Size

```sql
PRAGMA mmap_size;
```

### Setting mmap Size

```sql
PRAGMA mmap_size = 268435456;
```

(256 MB)

### Observation

- mmap enables memory-mapped I/O.
- Reduces repeated disk reads.
- Improves read-heavy query performance.

---

## Timing Queries Without mmap

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

### Observation

- Query execution takes comparatively more time.

---

## Timing Queries With mmap

Enable mmap:

```sql
PRAGMA mmap_size = 268435456;
```

Run:

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

### Observation

- Slightly faster execution for repeated reads.
- Improvement is more noticeable for large databases.

---

## Checking SQLite Process

```bash
ps aux | grep sqlite
```

### Observation

- SQLite runs as an embedded database.
- No separate database server process exists.

---

# 2. PostgreSQL Exploration

## Installation

```bash
sudo apt install postgresql postgresql-contrib
```

## Starting PostgreSQL

```bash
sudo systemctl start postgresql
```

## Access PostgreSQL

```bash
sudo -u postgres psql
```

---

## Creating Database

```sql
CREATE DATABASE labdb;
```

Connect:

```sql
\c labdb
```

Create table:

```sql
CREATE TABLE users(
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INT
);
```

Insert data:

```sql
INSERT INTO users(name, age)
VALUES
('Alice',21),
('Bob',22),
('Charlie',23);
```

---

## Finding Page Size

```sql
SHOW block_size;
```

### Output

```text
8192
```

### Observation

- PostgreSQL default page size is 8KB.

---

## Finding Page Count

Install extension:

```sql
CREATE EXTENSION pgstattuple;
```

Check table stats:

```sql
SELECT * FROM pgstattuple('users');
```

### Observation

- PostgreSQL stores data in multiple pages internally.
- Page statistics include free space and tuple usage.

---

## Timing Queries

```bash
time psql -d labdb -c "SELECT * FROM users;"
```

### Observation

- PostgreSQL query execution includes server communication overhead.
- Better optimized for concurrent queries.

---

## Checking PostgreSQL Processes

```bash
ps aux | grep postgres
```

### Observation

- PostgreSQL runs as a dedicated server process.
- Multiple background worker processes are visible.

---

# 3. SQLite3 vs PostgreSQL Comparison

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | Embedded DB | Client-Server DB |
| Default Page Size | 4KB | 8KB |
| Storage | Single file | Multiple internal files |
| Server Process | No | Yes |
| mmap Support | Yes | Limited internal use |
| Best For | Lightweight apps | Large scalable systems |
| Query Performance | Fast for small/local workloads | Better for concurrent workloads |
| Concurrency | Limited | High |
| Setup Complexity | Very Easy | Moderate |

---

# mmap Impact Analysis

## SQLite

### Without mmap

- More disk I/O
- Slower repeated reads

### With mmap

- Uses virtual memory mapping
- Faster read performance
- Reduced syscall overhead

## PostgreSQL

- PostgreSQL internally manages shared buffers and caching.
- mmap tuning is generally less exposed to users.

---

# Final Observations

## SQLite3

Advantages:
- Lightweight
- Easy setup
- Single portable file
- Very fast for local applications

Disadvantages:
- Limited concurrency
- Not ideal for high-scale production systems

---

## PostgreSQL

Advantages:
- Powerful query optimizer
- Excellent concurrency support
- Suitable for enterprise applications

Disadvantages:
- More resource usage
- Requires server management

---

# Conclusion

SQLite3 is ideal for embedded and lightweight applications, while PostgreSQL is better suited for scalable multi-user systems. mmap significantly improves SQLite read performance by reducing disk access overhead.