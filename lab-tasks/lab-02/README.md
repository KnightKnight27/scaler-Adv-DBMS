# Lab - 02 Assignment

**Name:** THRISHAL DOMA  
**ID:** 24BCS10097

---

# SQLite3 vs PostgreSQL Comparison

## SQLite3 Exploration

### Commands Used

```bash
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
time sqlite3 sample.db "SELECT * FROM users;"
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;"
ps aux | grep sqlite
```

### SQLite3 Observations

#### Page Information
- **Page Size**: 4096 bytes
- **Page Count**: 2 pages

#### mmap Information

Default mmap was checked using:
```sql
PRAGMA mmap_size;
```

mmap was changed to:
```sql
PRAGMA mmap_size = 268435456;
```

#### Query Execution Time

**Without mmap:**
```
real    0m0.003s
user    0m0.000s
sys     0m0.000s
```

**With mmap:**
```
real    0m0.002s
user    0m0.000s
sys     0m0.001s
```

### Analysis
- SQLite3 stores the entire database in a single file.
- The page size is relatively small and suitable for lightweight databases.
- Enabling mmap slightly improved query execution time.
- SQLite3 is simple and fast for small-scale applications.

## PostgreSQL Exploration

### Commands Used

```sql
SHOW block_size;

SELECT relpages, reltuples
FROM pg_class
WHERE relname='users';

\timing

SELECT * FROM users;
```

### PostgreSQL Observations

#### Storage Information
- **Block Size**: 8192 bytes
- **relpages**: 0

#### Query Execution Time
- **Time**: 0.270 ms

### Analysis
- PostgreSQL uses larger block sizes compared to SQLite3.
- PostgreSQL is designed for large-scale and multi-user systems.
- It provides advanced database features and better concurrency handling.
- Query execution was very fast even with additional server overhead.

## Comparison Between SQLite3 and PostgreSQL

| Feature | SQLite3 | PostgreSQL |
|---------|---------|------------|
| Database Type | File-based | Client-Server |
| Page Size | 4096 bytes | 8192 bytes |
| Page Count | 2 | 0 relpages |
| Query Performance | 0.003s | 0.270 ms |
| mmap Support | Yes | Not tested |
| Setup Complexity | Very Easy | Moderate |
| Best Use Case | Small Applications | Large Applications |
| Concurrency Support | Limited | Excellent |

## mmap Impact

### SQLite3 mmap Impact
- mmap reduced the query execution time slightly.
- Memory mapping allows SQLite3 to access database files more efficiently.
- Performance improvements become more noticeable with larger databases.

## Conclusion

**SQLite3** is lightweight, easy to use, and suitable for small projects or embedded applications. It requires minimal setup and provides fast performance for simple workloads.

**PostgreSQL** is a powerful relational database system designed for scalability, reliability, and multi-user environments. Although it requires more setup, it offers advanced features and better concurrency support.

### Key Findings from the Experiments:

- SQLite3 showed slightly faster execution for this small dataset.
- PostgreSQL provides better architecture for enterprise-level applications.
- mmap improved SQLite3 query performance marginally.

Both databases are efficient in their respective use cases.
