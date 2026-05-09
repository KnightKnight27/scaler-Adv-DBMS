# Database Comparison Report — SQLite3 vs PostgreSQL

## 1) SQLite3 Exploration

### Commands Used

```bash
sqlite3 sample.db
ls -lh
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size=268435456;
time sqlite3 sample.db "SELECT * FROM users;" > /dev/null
ps aux | grep sqlite
```

### Observations

- Database file size: **3.7 MB**
- Page size: **4096 bytes (4 KB)**
- Page count: **931**
- mmap default value: **0**
- mmap changed to: **268435456 bytes (256 MB)**

### Query Performance

With mmap:

```text
real 0m0.051s
user 0m0.042s
sys  0m0.008s
```

Without mmap:

```text
real 0m0.052s
user 0m0.048s
sys  0m0.004s
```

### mmap Impact

For this small database, mmap had **negligible impact** on performance.

### Process Observation

SQLite runs as a **single lightweight process** with low CPU and memory usage.

---

# 2) PostgreSQL Exploration

### Commands Used

```bash
sudo systemctl start postgresql
sudo -u postgres psql
CREATE DATABASE labdb;
\c labdb
SHOW block_size;
SELECT relpages FROM pg_class WHERE relname='users';
\timing
SELECT * FROM users;
ps aux | grep postgres
```

### Observations

- Block/Page size: **8192 bytes (8 KB)**
- Page count: **824**
- Approximate table storage: **6.4 MB**

### Query Performance

```text
Time: 37.815 ms
```

### Process Observation

PostgreSQL runs multiple background processes:

- Main server process
- Checkpointer
- Background writer
- WAL writer
- Autovacuum launcher
- Logical replication launcher
- Client connection process

This makes PostgreSQL heavier but more capable.

---

# 3) Comparison Analysis

| Metric | SQLite3 | PostgreSQL |
|--------|---------|------------|
| Architecture | File-based | Server-based |
| Page Size | 4096 bytes | 8192 bytes |
| Page Count | 931 | 824 |
| Storage Used | 3.7 MB | 6.4 MB |
| Query Performance | ~51 ms | ~38 ms |
| mmap Support | Yes | Managed internally |
| Background Processes | Single | Multiple |
| Resource Usage | Low | Higher |

---

# Conclusion

## SQLite3
Advantages:

- Lightweight
- Minimal memory usage
- No server required
- Easy setup
- Good for embedded/local applications

Disadvantages:

- Limited concurrency
- Fewer advanced features

## PostgreSQL
Advantages:

- Faster query execution
- Better concurrency support
- Advanced indexing and optimization
- Enterprise-grade features

Disadvantages:

- Higher memory usage
- More complex setup
- Runs multiple background services

Overall:

- **SQLite3** is best for small/local applications.
- **PostgreSQL** is best for larger, production-grade systems.