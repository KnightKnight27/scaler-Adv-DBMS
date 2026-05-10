# SQLite3 vs PostgreSQL Lab Report

## Student Details
- Name: Aditya Vikram Singh
- Role Number: 10429

---

# Database Systems Lab: SQLite3 vs PostgreSQL

## 1. SQLite3 Exploration

### Installation

Ubuntu/Debian:

```bash
sudo apt update
sudo apt install sqlite3
```

Check version:

```bash
sqlite3 --version
```

---

## Creating Sample Database

Create database:

```bash
sqlite3 sample.db
```

Create table:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

Insert sample data:

```sql
INSERT INTO users (name, age) VALUES
('Alice', 22),
('Bob', 25),
('Charlie', 21),
('David', 30),
('Eva', 24);
```

Exit SQLite:

```sql
.exit
```

---

## Observing File Size

Command used:

```bash
ls -lh
```

Observation:

```text
sample.db size was around 8 KB
```

SQLite stores the complete database inside a single file.

---

## Finding Page Size

Open database:

```bash
sqlite3 sample.db
```

Run:

```sql
PRAGMA page_size;
```

Output observed:

```text
4096
```

Observation:
- SQLite default page size is usually 4096 bytes (4 KB).

---

## Finding Page Count

Command:

```sql
PRAGMA page_count;
```

Output observed:

```text
2
```

Observation:
- Total pages increase as database size increases.

---

## mmap_size Experiment

Check current mmap size:

```sql
PRAGMA mmap_size;
```

Set mmap size:

```sql
PRAGMA mmap_size = 268435456;
```

(256 MB)

Verify:

```sql
PRAGMA mmap_size;
```

Observation:
- SQLite enabled memory-mapped I/O.
- Query execution became slightly faster for repeated reads.

---

## Query Timing Without mmap

Command:

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

Observed result:

```text
real    0m0.005s
user    0m0.002s
sys     0m0.001s
```

---

## Query Timing With mmap

Enable mmap:

```bash
sqlite3 sample.db
```

```sql
PRAGMA mmap_size = 268435456;
```

Then run:

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

Observed result:

```text
real    0m0.003s
user    0m0.001s
sys     0m0.001s
```

Observation:
- mmap slightly reduced execution time.
- Improvement is more noticeable with larger databases.

---

## Checking SQLite Process

Command:

```bash
ps aux | grep sqlite
```

Observation:
- SQLite runs as a lightweight process.
- No separate database server process exists.

---

# 2. PostgreSQL Setup

## Installation

Ubuntu/Debian:

```bash
sudo apt install postgresql postgresql-contrib
```

Check version:

```bash
psql --version
```

---

## Start PostgreSQL Service

```bash
sudo systemctl start postgresql
sudo systemctl status postgresql
```

---

## Create Database

Switch to postgres user:

```bash
sudo -i -u postgres
```

Open PostgreSQL shell:

```bash
psql
```

Create database:

```sql
CREATE DATABASE labdb;
```

Connect database:

```sql
\c labdb
```

---

## Create Table

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    age INT
);
```

Insert sample data:

```sql
INSERT INTO users(name, age) VALUES
('Alice',22),
('Bob',25),
('Charlie',21),
('David',30),
('Eva',24);
```

---

## Finding PostgreSQL Page Size

Command:

```sql
SHOW block_size;
```

Observed output:

```text
8192
```

Observation:
- PostgreSQL default page size is 8192 bytes (8 KB).

---

## Finding Page Count

Command:

```sql
SELECT relpages FROM pg_class WHERE relname='users';
```

Observed output:

```text
1
```

Observation:
- PostgreSQL internally manages storage in pages.

---

## Query Execution Time

Enable timing:

```sql
\timing
```

Run query:

```sql
SELECT * FROM users;
```

Observed result:

```text
Time: 1.2 ms
```

Observation:
- PostgreSQL query execution was slightly slower for tiny datasets due to server overhead.
- PostgreSQL performs better for large concurrent workloads.

---

## Checking PostgreSQL Process

Command:

```bash
ps aux | grep postgres
```

Observation:
- Multiple PostgreSQL background processes were running.
- PostgreSQL uses a client-server architecture.

---

# 3. Comparison Analysis

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | File-based | Client-server |
| Default Page Size | 4096 bytes | 8192 bytes |
| Page Count | Small for tiny DB | Managed internally |
| mmap Support | Yes | Not directly exposed like SQLite |
| Query Speed (small DB) | Faster | Slight overhead |
| Concurrency | Limited | Excellent |
| Setup Complexity | Very easy | Moderate |
| Best Use Case | Small/local apps | Large scalable systems |

---

# mmap Impact Analysis

## SQLite3

- mmap improved read performance slightly.
- Reduced disk I/O by mapping database pages directly into memory.
- More beneficial for large databases and repeated reads.

---

## PostgreSQL

- PostgreSQL already uses advanced internal caching mechanisms.
- mmap is not commonly configured manually like SQLite.

---

# Final Observations

## SQLite3

### Advantages

- Lightweight
- Simple setup
- Fast for local applications

### Disadvantages

- Limited concurrency
- Not ideal for large multi-user systems

---

## PostgreSQL

### Advantages

- Powerful and scalable
- Better concurrency
- Advanced features

### Disadvantages

- More resource usage
- More setup complexity

---

# Conclusion

SQLite3 is best suited for lightweight applications, embedded systems, and local storage because it is simple and fast.

PostgreSQL is better for enterprise applications and large-scale systems because it provides better concurrency, scalability, and advanced database features.

For small queries, SQLite3 performed slightly faster. PostgreSQL introduced some overhead because it runs as a dedicated database server.

Memory mapping (`mmap`) in SQLite improved query performance slightly by reducing file access overhead.# scaler-Adv-DBMS
