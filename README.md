# DBMS Lab Assignment 1 - Comparison Report

## 1. SQLite3 Observations

### Database Setup
- Database file: `sample.db`
- Tables: users (10000), products (5000), orders (20000), reviews (15000)

### File Size
```
-rw-r--r-- 1 vedu vedu 3.1M May  9 12:14 sample.db
```

### Page Size
```
sqlite3 sample.db "PRAGMA page_size;"
4096
```

### Page Count
```
sqlite3 sample.db "PRAGMA page_count;"
793
```

### mmap_size Experiments
```
PRAGMA mmap_size;  (default)
0
```

### Query Timing Results

| Query | Without mmap (mmap_size=0) | With mmap (256MB) | With mmap (64MB) |
|-------|---------------------------|-------------------|------------------|
| SELECT * FROM users | 0.016s | 0.004s | N/A |
| SELECT * FROM orders | 0.039s | 0.037s | 0.034s |

**Commands Used:**
```bash
ls -lh sample.db
sqlite3 sample.db "PRAGMA page_size;"
sqlite3 sample.db "PRAGMA page_count;"
sqlite3 sample.db "PRAGMA mmap_size;"
sqlite3 sample.db "PRAGMA mmap_size = 268435456;"  # Enable 256MB mmap
sqlite3 sample.db "PRAGMA mmap_size = 0;"           # Disable mmap
time sqlite3 sample.db "SELECT * FROM users;"
ps aux | grep sqlite
```

### Observations
- Page size: 4096 bytes (4 KB)
- With mmap enabled, small queries showed slight improvement (0.016s → 0.004s for users table)
- Larger result sets (orders 20000 rows) showed marginal improvement
- mmap_size=0 means memory-mapped I/O is disabled by default

---

## 2. PostgreSQL Observations

### Database Setup
- Database: `sample_db`
- Tables: users (10000), products (5000), orders (20000), reviews (15000)

### Page Size
```sql
SHOW block_size;
8192
```

### Page Count
```sql
SELECT relpages, relname FROM pg_class WHERE relname IN ('users', 'products', 'orders', 'reviews');

 relpages | relname
----------+----------
       94 | users
       42 | products
      148 | orders
      141 | reviews
```

### Query Timing (EXPLAIN ANALYZE)
```sql
EXPLAIN ANALYZE SELECT * FROM users LIMIT 1000;
-- Execution Time: 0.380 ms

EXPLAIN ANALYZE SELECT * FROM orders LIMIT 1000;
-- Execution Time: 0.374 ms
```

**Commands Used:**
```bash
sudo -u postgres psql -d sample_db
SHOW block_size;
SELECT relpages, relname FROM pg_class WHERE relname IN ('users', 'products', 'orders', 'reviews');
EXPLAIN ANALYZE SELECT * FROM users LIMIT 1000;
ps aux | grep postgres
```

### Observations
- Block size (page size): 8192 bytes (8 KB)
- Total pages: users (94) + products (42) + orders (148) + reviews (141) = 425 pages
- Storage: 425 pages × 8KB = ~3.4 MB
- Query execution time: ~0.37-0.38ms for LIMIT 1000 queries

---

## 3. Comparison Analysis

### Page Size Comparison

| Database | Page Size |
|----------|-----------|
| SQLite3  | 4096 bytes (4 KB) |
| PostgreSQL | 8192 bytes (8 KB) |

**Analysis:** PostgreSQL uses twice the page size compared to SQLite3. This affects:
- Memory efficiency for different workloads
- I/O operations (larger pages = fewer I/O operations for large reads)
- Storage overhead (larger pages may have more internal fragmentation)

### Page Count Comparison

| Database | Table | Records | Pages | Total Size |
|----------|-------|---------|-------|------------|
| SQLite3 | users | 10000 | 793 (total) | 3.1 MB |
| PostgreSQL | users | 10000 | 94 | 752 KB |
| PostgreSQL | products | 5000 | 42 | 336 KB |
| PostgreSQL | orders | 20000 | 148 | 1.2 MB |
| PostgreSQL | reviews | 15000 | 141 | 1.1 MB |
| PostgreSQL | **Total** | 47000 | **425** | **~3.4 MB** |

**Analysis:** Both databases store ~same data in similar space. PostgreSQL pages are 8KB vs SQLite's 4KB, resulting in fewer pages for equivalent data.

### Query Performance Comparison

| Database | Query | Time |
|----------|-------|------|
| SQLite3 (no mmap) | SELECT * FROM users (10000) | 16ms |
| SQLite3 (256MB mmap) | SELECT * FROM users (10000) | 4ms |
| PostgreSQL | SELECT * FROM users LIMIT 1000 | 0.38ms |
| PostgreSQL | SELECT * FROM orders LIMIT 1000 | 0.37ms |

**Analysis:**
- PostgreSQL shows faster query times (~0.38ms) for limited result sets
- SQLite3 with mmap shows 4x improvement (16ms → 4ms) for full table scans
- PostgreSQL's server architecture and caching provide optimization advantages
- SQLite3 is faster for full table scans when mmap is enabled

### mmap Impact on SQLite3

| mmap_size | Query Time (orders) |
|-----------|---------------------|
| 0 (disabled) | 0.039s |
| 67108864 (64MB) | 0.034s |
| 268435456 (256MB) | 0.037s |

**Analysis:**
- mmap provides marginal improvement for larger datasets
- Optimal mmap size may vary by workload
- Performance gain depends on data size and query patterns

---

## 4. Key Differences Summary

| Feature | SQLite3 | PostgreSQL |
|---------|---------|------------|
| Type | Embedded DB (file-based) | Server-based DB |
| Page Size | 4 KB | 8 KB |
| Architecture | Single file | Client-server |
| mmap support | Yes (configurable) | N/A (uses OS cache) |
| Query Performance | Good for small-medium | Excellent for large |

---

## 5. Commands Summary

### SQLite3
```bash
sqlite3 --version
sqlite3 sample.db "PRAGMA page_size;"
sqlite3 sample.db "PRAGMA page_count;"
sqlite3 sample.db "PRAGMA mmap_size = 268435456;"
time sqlite3 sample.db "SELECT * FROM users;"
ls -lh sample.db
ps aux | grep sqlite
```

### PostgreSQL
```bash
sudo -u postgres psql -d sample_db
SHOW block_size;
SELECT relpages, relname FROM pg_class WHERE relname = 'users';
\timing
SELECT * FROM users;
ps aux | grep postgres
```

---

## 6. Conclusion

- **Page Size:** PostgreSQL uses 8KB pages vs SQLite3's 4KB - PostgreSQL is better optimized for large sequential reads
- **Query Performance:** PostgreSQL faster for small queries, but SQLite3 with mmap shows significant improvement
- **mmap Impact:** SQLite3 mmap provides 2-4x speedup for certain workloads, but effect varies with data size
- **Architecture:** SQLite3 is better for embedded/mobile apps, PostgreSQL for enterprise applications