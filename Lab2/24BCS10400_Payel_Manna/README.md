# Database Systems Lab Report

## Name: Payel Manna
## Role Number: 24BCS10400

---

# 1. SQLite3 Exploration

## Installation

SQLite3 was installed using:

```bash
sudo apt install sqlite3
```

---

# Database Creation

A sample database named `sample.db` was created with a `users` table containing 100000 rows.

---

# Database File Information

## Command Used

```bash
ls -lh
```

## Output

```bash
-rw-r--r-- 1 payel-manna payel-manna 2.0M May 9 13:23 sample.db
```

## Observation

- SQLite stores the entire database inside a single file.
- Database file size was approximately **2.0 MB**.

---

# Page Information

## Commands Used

```sql
PRAGMA page_size;
PRAGMA page_count;
```

## Output

```sql
page_size = 4096
page_count = 488
```

## Observations

- SQLite page size is **4096 bytes (4 KB)**.
- Total allocated pages = **488**.

Approximate database size calculation:

```text
4096 × 488 = 1,998,848 bytes ≈ 2 MB
```

The calculated size closely matches the actual file size.

---

# mmap_size Experiment

## Without mmap

### Command Used

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

### Output

```text
real    0m0.250s
user    0m0.077s
sys     0m0.162s
```

---

## With mmap

### Commands Used

```sql
PRAGMA mmap_size = 268435456;
```

Then:

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

### Output

```text
real    0m0.241s
user    0m0.078s
sys     0m0.152s
```

---

# mmap Observation

- Query execution with mmap was slightly faster.
- Difference was very small because the database size was small (2 MB).
- The operating system cache already optimizes file access efficiently.
- mmap is more beneficial for large databases and repeated read-heavy workloads.

---

# SQLite Process Observation

## Command Used

```bash
ps aux | grep sqlite
```

## Output

```bash
payel-m+   15030  0.0  0.0   9152  2356 pts/0    S+   13:35   0:00 grep --color=auto sqlite
```

## Observation

- No dedicated SQLite server process was running.
- SQLite works as an embedded database inside the application itself.
- SQLite does not follow a client-server architecture.

---

# 2. PostgreSQL Exploration

## Installation

PostgreSQL was installed using:

```bash
sudo apt install postgresql postgresql-contrib
```

---

# Database Setup

A database named `labdb` was created with a `users` table containing 100000 rows.

---

# Block Size Information

## Command Used

```sql
SHOW block_size;
```

## Output

```sql
8192
```

## Observation

- PostgreSQL block size is **8192 bytes (8 KB)**.
- PostgreSQL pages are larger than SQLite pages.

---

# Page Count Information

## Command Used

```sql
SELECT relpages
FROM pg_class
WHERE relname = 'users';
```

## Output

```sql
636
```

## Observation

- Total allocated pages for the `users` table = **636**.

---

# Query Execution Time

## Commands Used

```sql
\timing
SELECT * FROM users;
```

## Output

```text
Time: 16.870 ms
```

## Observation

- PostgreSQL executed the query very efficiently.
- PostgreSQL uses advanced caching and optimization mechanisms.

---

# PostgreSQL Process Observation

## Command Used

```bash
ps aux | grep postgres
```

## Output

```bash
postgres    1534  0.0  0.3 226336 29304 ?        Ss   13:06   0:00 /usr/lib/postgresql/17/bin/postgres -D /var/lib/postgresql/17/main -c config_file=/etc/postgresql/17/main/postgresql.conf
postgres    1544  0.0  0.3 226620 28892 ?        Ss   13:06   0:00 postgres: 17/main: checkpointer
postgres    1545  0.0  0.0 226492  6324 ?        Ss   13:06   0:00 postgres: 17/main: background writer
postgres    1548  0.0  0.1 226336  9012 ?        Ss   13:06   0:00 postgres: 17/main: walwriter
postgres    1550  0.0  0.0 227912  7516 ?        Ss   13:06   0:00 postgres: 17/main: autovacuum launcher
postgres    1551  0.0  0.0 227920  6772 ?        Ss   13:06   0:00 postgres: 17/main: logical replication launcher
```

## Observation

- PostgreSQL runs multiple background processes.
- It follows a full client-server architecture.
- Dedicated processes handle:
  - Checkpointing
  - Background writing
  - WAL writing
  - Autovacuum
  - Replication

---

# 3. SQLite3 vs PostgreSQL Comparison

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | Embedded Database | Client-Server Database |
| Storage Type | Single File | Multiple Managed Files |
| Page Size | 4096 bytes | 8192 bytes |
| Page Count | 488 | 636 |
| Query Time | ~0.25 s | ~16.87 ms |
| mmap Support | Explicit mmap_size support | Internal shared buffer management |
| Server Process | No dedicated process | Multiple background processes |
| Scalability | Best for small/local apps | Best for large scalable systems |
| Concurrency | Limited | High concurrency support |

---

# 4. Final Analysis

## SQLite3

Advantages:
- Lightweight
- Easy setup
- No server required
- Good for embedded and local applications

Disadvantages:
- Limited concurrency
- Less scalable for large systems

---

## PostgreSQL

Advantages:
- Powerful query optimization
- Better concurrency handling
- Advanced process management
- Suitable for production-scale applications

Disadvantages:
- More resource usage
- Requires server setup and management

---

# 5. Conclusion

SQLite is simple, lightweight, and ideal for embedded or local applications where minimal setup is required.

PostgreSQL is a more advanced relational database system that provides better scalability, concurrency, and performance optimization for enterprise and production workloads.

The experiments showed that PostgreSQL uses a larger page size and multiple background processes, while SQLite operates directly through a single database file without a separate server process.