
# Database Internals Lab Report
# SQLite3 vs PostgreSQL: Storage, MMAP, Processes, and Query Performance

## Prepared By
**Name:** Akarsh Garg  
**Course:** Advanced Database Management Systems (ADBMS)

---

# 1. Objective

The objective of this lab experiment was to perform a detailed comparison between SQLite3 and PostgreSQL using practical experiments on identical datasets.

The comparison focused on:

- Storage structure
- Page and block management
- Query execution performance
- Memory-mapped I/O (MMAP)
- Buffer management
- Process architecture
- Scalability and concurrency

The databases were tested using both small datasets and large datasets containing up to 1,000,000 rows.

---

# 2. Environment Setup

| Component | Details |
|---|---|
| Operating System | macOS (Darwin ARM64) |
| SQLite Version | SQLite 3.51.0 |
| PostgreSQL Version | PostgreSQL 16/18 |
| Installation Method | Homebrew |
| SQLite Database | sample.db |
| PostgreSQL Database | labdb |

---

# 3. Introduction to SQLite3

SQLite3 is a lightweight embedded relational database engine. Unlike traditional databases, SQLite does not require a dedicated server process.

SQLite operates directly inside the application process and stores the entire database in a single `.db` file.

## Features of SQLite

- Serverless architecture
- Zero configuration
- Single-file storage
- Lightweight memory usage
- ACID-compliant transactions
- Fast local database access

## Common Applications

- Android applications
- iOS applications
- Browser storage
- Embedded systems
- Desktop applications

---

# 4. SQLite Database Creation

## SQLite Table Creation

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT,
    age INTEGER,
    city TEXT,
    created TEXT
);
```

## SQLite Dataset Generation

```sql
WITH RECURSIVE seq(n) AS (
    SELECT 1
    UNION ALL
    SELECT n + 1 FROM seq WHERE n < 1000000
)
INSERT INTO users
SELECT
    n,
    'User_' || n,
    'user' || n || '@example.com',
    20 + (n % 60),
    CASE (n % 5)
        WHEN 0 THEN 'Bangalore'
        WHEN 1 THEN 'Mumbai'
        WHEN 2 THEN 'Delhi'
        WHEN 3 THEN 'Chennai'
        ELSE 'Hyderabad'
    END,
    datetime('now');
```

## SQLite Index Creation

```sql
CREATE INDEX idx_users_city ON users(city);
CREATE INDEX idx_users_age ON users(age);
```

The dataset simulated realistic user information including names, email addresses, ages, cities, and timestamps.

---

# 5. SQLite Storage Analysis

SQLite internally stores information using fixed-size pages.

## Commands Used

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA freelist_count;
PRAGMA mmap_size;
```

## Observed Results

| Metric | Value |
|---|---|
| Page Size | 4096 bytes |
| Page Count | 3986 to 10605 |
| File Size | 16 MB to 41 MB |
| Default mmap_size | 0 |

## Formula Verification

```text
Database Size = Page Size × Page Count
```

Example:

```text
4096 × 10605 ≈ 41 MB
```

This verified that SQLite organizes the database using fixed-size pages.

Advantages of smaller pages:

- Better random read efficiency
- Reduced memory wastage
- Better embedded-system performance

---

# 6. SQLite MMAP Experiment

SQLite supports memory-mapped I/O using the `mmap()` system call.

## Disable MMAP

```sql
PRAGMA mmap_size = 0;
```

## Enable MMAP

```sql
PRAGMA mmap_size = 268435456;
```

## Query Tested

```sql
SELECT * FROM users;
```

## Timing Commands

```bash
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null

time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null
```

## Performance Results

| Dataset | Without MMAP | With MMAP |
|---|---|---|
| Small Dataset | 0.011 s | 0.018 s |
| Large Dataset | ~0.478–0.527 s | ~0.329–0.461 s |

## Analysis

For small datasets, MMAP produced little improvement because the operating system cache already stored most of the data in memory.

For larger datasets, MMAP significantly improved performance by reducing:

- System calls
- Buffer copying
- Kernel-user memory transfer overhead

MMAP benefits become more visible in:

- Large databases
- Read-heavy workloads
- Sequential scans

---

# 7. SQLite Process Architecture

## Command Used

```bash
ps aux | grep sqlite
```

## Observation

SQLite does not run a dedicated server process.

Instead:

- The database engine runs inside the application process.
- No background workers are required.
- Memory usage remains extremely low.

## Advantages

- Very low overhead
- Minimal setup
- Lightweight execution

## Limitations

- Limited concurrency
- Single-writer restriction
- Less suitable for enterprise-scale workloads

---

# 8. Introduction to PostgreSQL

PostgreSQL is a powerful open-source enterprise relational database management system.

Unlike SQLite, PostgreSQL follows a client-server architecture where multiple users connect to a dedicated database server.

## Features of PostgreSQL

- Multi-user concurrency
- WAL-based crash recovery
- Parallel query execution
- Advanced indexing
- Replication support
- Enterprise scalability

## Common Applications

- Banking systems
- SaaS platforms
- Analytics systems
- Enterprise applications

---

# 9. PostgreSQL Installation and Setup

## Installation Commands

```bash
brew install postgresql
brew services start postgresql
```

## Version Check

```bash
psql --version
```

---

# 10. PostgreSQL Database Creation

## Create Database

```sql
CREATE DATABASE labdb;
```

## PostgreSQL Table Creation

```sql
CREATE TABLE users (
    id BIGINT PRIMARY KEY,
    name TEXT,
    email TEXT,
    age INT,
    city TEXT,
    created TIMESTAMP
);
```

## PostgreSQL Dataset Generation

```sql
INSERT INTO users
SELECT
    g,
    'User_' || g,
    'user' || g || '@example.com',
    20 + (g % 60),
    'Bangalore',
    NOW()
FROM generate_series(1, 1000000) g;
```

## PostgreSQL Index Creation

```sql
CREATE INDEX idx_users_city ON users(city);
CREATE INDEX idx_users_age ON users(age);
```

---

# 11. PostgreSQL Storage Analysis

PostgreSQL stores information internally using fixed-size blocks called pages.

## Commands Used

```sql
SHOW block_size;

SELECT pg_relation_size('users');

SELECT pg_relation_size('users') / 8192;
```

## Observed Results

| Metric | Value |
|---|---|
| Block Size | 8192 bytes |
| Heap Pages | 1173–8334 |
| Table Size | ~13 MB |
| Database Size | ~20 MB |

## Analysis

PostgreSQL required fewer pages than SQLite because PostgreSQL uses larger 8 KB blocks compared to SQLite’s 4 KB pages.

Advantages:

- Better sequential throughput
- Fewer page lookups
- Better scan performance

---

# 12. PostgreSQL Buffer Management

## Commands Used

```sql
SHOW shared_buffers;

SHOW effective_cache_size;
```

## Observed Values

| Parameter | Value |
|---|---|
| shared_buffers | 128 MB |
| effective_cache_size | 4 GB |

PostgreSQL uses centralized memory management through shared buffer pools instead of directly exposing MMAP configuration.

Benefits:

- Faster repeated queries
- Better concurrency handling
- Controlled caching behavior
- Efficient transaction management

---

# 13. PostgreSQL Query Performance

## Query Tested

```sql
SELECT * FROM users;
```

## Timing Results

| Dataset | Time |
|---|---|
| Small Dataset | ~0.559 ms |
| Medium Dataset | ~15–19 ms |
| Large Dataset | ~0.75–3.02 sec |

## Analysis

PostgreSQL introduced additional overhead because of:

- Client-server communication
- Query planning
- Background worker coordination

However, PostgreSQL supports:

- Parallel execution
- Worker processes
- Advanced optimization

This makes PostgreSQL significantly more scalable for enterprise workloads.

---

# 14. PostgreSQL Process Architecture

## Command Used

```bash
ps aux | grep postgres
```

## Processes Observed

- Checkpointer
- Background writer
- WAL writer
- Autovacuum launcher
- Logical replication launcher
- I/O workers

These services provide:

- Crash recovery
- Automatic maintenance
- Replication support
- Concurrent transaction handling

Compared to SQLite, PostgreSQL uses more memory and CPU resources but provides enterprise-grade reliability.

---

# 15. Detailed SQLite3 vs PostgreSQL Comparison

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | Embedded | Client-Server |
| Storage Style | Single .db file | Multiple relation files |
| Page Size | 4 KB | 8 KB |
| MMAP Support | Direct PRAGMA support | Internal buffer management |
| Parallel Query Support | No | Yes |
| Concurrency | Limited | Strong |
| Resource Usage | Very Low | Moderate to High |
| Scalability | Moderate | Excellent |

---

# 16. Critical Analysis

The experiments clearly demonstrated that SQLite and PostgreSQL are optimized for different environments.

## SQLite Prioritizes

- Simplicity
- Portability
- Lightweight execution
- Embedded deployment

SQLite eliminates:

- Network overhead
- Background workers
- Complex process coordination

This allowed SQLite to perform extremely well for lightweight local workloads.

## PostgreSQL Prioritizes

- Reliability
- Scalability
- Multi-user concurrency
- Enterprise-grade performance

Although PostgreSQL introduced additional overhead during small benchmarks, it provided advanced features unavailable in SQLite such as:

- Parallel execution
- WAL-based recovery
- Replication
- Advanced indexing
- Background maintenance

---

# 17. Real-World Industry Usage

## SQLite Used By

- Google
- Apple
- Mozilla

Applications include:

- Android devices
- iOS systems
- Browsers
- Embedded hardware

## PostgreSQL Used By

- Instagram
- Spotify
- Reddit
- NASA

Applications include:

- Banking systems
- Cloud platforms
- Enterprise SaaS systems
- Analytics platforms

---

# 18. Final Conclusion

The experiments clearly demonstrated that SQLite3 and PostgreSQL are optimized for different environments.

## SQLite is Best For:

- Embedded applications
- Mobile devices
- Lightweight local software
- Simple deployment

## PostgreSQL is Best For:

- Enterprise systems
- High concurrency workloads
- Large-scale applications
- Cloud and analytics systems

## Final Observations

- SQLite performed faster for lightweight local scans.
- MMAP improved SQLite performance for larger datasets.
- PostgreSQL provided superior scalability and concurrency handling.

Both systems are highly optimized within their intended domains, and the correct choice depends entirely on application requirements.

---

# 19. References

1. SQLite Official Documentation – https://www.sqlite.org/docs.html  
2. PostgreSQL Official Documentation – https://www.postgresql.org/docs/  
3. SQLite PRAGMA Documentation  
4. PostgreSQL EXPLAIN ANALYZE Documentation  
5. PostgreSQL WAL Documentation  
6. PostgreSQL Shared Buffer Documentation  
