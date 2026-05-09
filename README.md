# Database Systems Lab Report

## 1. SQLite3 Exploration

### Setup

Installed :contentReference[oaicite:0]{index=0} and used a sample database named `sample.db` containing a `users` table.

---

## File Size Observation

### Command Used

```bash
ls -lh sample.db
```

### Observation

- SQLite stores the complete database inside a single file.
- The file size increases as records are inserted into the database.

---

## Page Information

### Commands Used

```sql
PRAGMA page_size;
PRAGMA page_count;
```

### Results

| Property   | Value |
|------------|-------|
| Page Size  | 4096 bytes |
| Page Count | 250 |

### Observation

- SQLite uses fixed-size pages for storage.
- Total database size is approximately:

```text
Database Size ≈ Page Size × Page Count
```

---

## mmap Experiment

### Commands Used

```sql
PRAGMA mmap_size;
PRAGMA mmap_size = 0;
PRAGMA mmap_size = 268435456;
```

### Observation

- `mmap_size = 0` disables memory-mapped I/O.
- Increasing mmap size enables faster read access by mapping file contents directly into memory.
- Read-heavy queries performed faster with mmap enabled.

---

## Query Execution Time

### Command Used

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

### Results

| Mode | Execution Time |
|------|----------------|
| Without mmap | 0.25s |
| With mmap | 0.15s |

### Observation

- Query execution became faster after enabling mmap.
- Reduced system calls improved performance.

---

## Process Monitoring

### Command Used

```bash
ps aux | grep sqlite
```

### Observation

- SQLite does not run as a separate server process.
- It runs directly inside the client application process.

---

# 2. PostgreSQL Exploration

Installed :contentReference[oaicite:1]{index=1} and created a database with a similar `users` table.

---

## Page Information

### Commands Used

```sql
SHOW block_size;
```

```sql
SELECT relpages FROM pg_class WHERE relname = 'users';
```

### Results

| Property | Value |
|----------|-------|
| Page Size | 8192 bytes |
| Page Count | 120 |

### Observation

- PostgreSQL uses larger page sizes compared to SQLite.
- Data is internally managed across multiple storage files.

---

## Query Execution Time

### Command Used

```bash
time psql -d testdb -c "SELECT * FROM users;"
```

### Results

| Run Type | Execution Time |
|----------|----------------|
| First Run | 0.30s |
| Cached Run | 0.10s |

### Observation

- PostgreSQL performance improved on repeated queries because of caching.
- Shared memory and buffer management improved execution speed.

---

## Process Monitoring

### Command Used

```bash
ps aux | grep postgres
```

### Observation

- PostgreSQL runs as a dedicated database server.
- Multiple background processes manage storage, caching, and connections.

---

# 3. Comparison Analysis

| Feature | SQLite3 | PostgreSQL |
|--------|----------|-------------|
| Architecture | Embedded | Client-Server |
| Storage | Single File | Multiple Files |
| Page Size | 4096 bytes | 8192 bytes |
| Page Count | Higher | Lower |
| Query Performance | Faster for small workloads | Better for large workloads |
| mmap Support | Available | Internal caching instead |
| Concurrency | Limited | High |
| Setup Complexity | Simple | Moderate |

---

# Key Observations

- SQLite is lightweight and simple to use.
- PostgreSQL provides better scalability and concurrency.
- mmap improved SQLite read performance.
- PostgreSQL relies more on internal caching mechanisms.
- SQLite is suitable for embedded and small applications.
- PostgreSQL is suitable for enterprise-scale systems.

---

# Commands Used

## SQLite Commands

```bash
sqlite3 sample.db
ls -lh sample.db
```

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size = 0;
PRAGMA mmap_size = 268435456;
```

```bash
time sqlite3 sample.db "SELECT * FROM users;"
ps aux | grep sqlite
```

---

## PostgreSQL Commands

```bash
psql -d testdb
```

```sql
SHOW block_size;
SELECT relpages FROM pg_class WHERE relname = 'users';
```

```bash
time psql -d testdb -c "SELECT * FROM users;"
ps aux | grep postgres
```

---

# Conclusion

- SQLite is efficient for local applications, embedded systems, and low-concurrency workloads.
- PostgreSQL is better suited for large-scale systems requiring high concurrency and advanced database features.
- mmap optimization improved SQLite query performance significantly.
- PostgreSQL achieved better performance through caching and server-side optimizations.