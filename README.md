# scaler-Adv-DBMS
# SQLite3 vs PostgreSQL Exploration Report

## Details
- Name: Rohan Jangam  
- Role Number: 24BCS10212

---

# 1. SQLite3 Exploration

## Setup
Used SQLite version 3.53.1 on Windows.

Then created a test.db and then created a table and added some mock data to test
---

## File Inspection

```bash
dir test.db
```

**Observation:**
- SQLite stores the database in a single file (`test.db`)
- File size increases as data grows

---

## PRAGMA Analysis

### Page Size
```sql
PRAGMA page_size;
```
```
╭───────────╮
│ page_size │
╞═══════════╡
│      4096 │
╰───────────╯
```
Output: 4096 bytes

### Page Count
```sql
PRAGMA page_count;
```
```
╭────────────╮
│ page_count │
╞════════════╡
│         47 │
╰────────────╯
```
Output: 47
### Database Size
```
Database Size = page_size × page_count
```

---

## mmap Experiment

### Default mmap
```sql
PRAGMA mmap_size;
```
Output: 0

### Enable mmap 
```sql
PRAGMA mmap_size = 268435456; --256mb
```
---

## Query Performance

### Without mmap
```powershell
Measure-Command { .\sqlite3 test.db "SELECT * FROM users;" }
```
Time: ~209 ms
```
Days              : 0
Hours             : 0
Minutes           : 0
Seconds           : 0
Milliseconds      : 209
Ticks             : 2093028
TotalDays         : 2.42248611111111E-06
TotalHours        : 5.81396666666667E-05
TotalMinutes      : 0.00348838
TotalSeconds      : 0.2093028
TotalMilliseconds : 209.3028
```

### With mmap
```powershell
Measure-Command { .\sqlite3 test.db "SELECT * FROM users;" }
```
Time: ~53 ms
```
Days              : 0
Hours             : 0
Minutes           : 0
Seconds           : 0
Milliseconds      : 53
Ticks             : 532377
TotalDays         : 6.16177083333333E-07
TotalHours        : 1.478825E-05
TotalMinutes      : 0.000887295
TotalSeconds      : 0.0532377
TotalMilliseconds : 53.2377
```
---

## Observations (SQLite)

- mmap improved performance significantly
- Execution time reduced from ~209 ms to ~53 ms (~4x faster)
- SQLite is embedded (no server)
- Best for small-scale or local applications
- Limited concurrency support

---

## mmap Impact

- Reduces disk I/O
- Uses memory mapping for faster access
- Fewer system calls → better performance

---

# 2. PostgreSQL Exploration

## Setup
Installed PostgreSQL and created a sample database.

---

## Page Analysis

### Page Size
```sql
SHOW block_size;
```
Output: 8192 bytes

### Page Count
```sql
SELECT relpages FROM pg_class WHERE relname = 'users';
```
Output: 41

Note: In PostgreSQL, relpages initially returned me 0 because statistics are not updated immediately after data insertion. Running ANALYZE updates the internal statistics, after which correct page count values can be retrieved.

---

## Query Performance
```sql
\timing
SELECT * FROM users;
```

- First Run: 16.131 ms  
- Second Run: 5.083 ms  

---

## Observations (PostgreSQL)

- Uses client-server architecture
- Handles multiple users efficiently
- Suitable for large-scale systems
- Slight overhead due to background services
- Query performance improves significantly on repeated execution due to caching

---

# 3. Comparison

| Feature            | SQLite3              | PostgreSQL            |
|-------------------|---------------------|----------------------|
| Architecture      | Embedded            | Client-Server        |
| Page Size         | 4096 bytes          | 8192 bytes           |
| Page Count        | 47                  | 41                   |
| Performance       | Fast (small data)   | Faster after caching |
| mmap Impact       | High                | Not applicable       |
| Concurrency       | Low                 | High                 |
| Setup             | Ez                | Mid             |

---

# Key Insights

- SQLite is ideal for lightweight and local use
- PostgreSQL is better for scalable and production systems
- mmap significantly improves SQLite read performance
- PostgreSQL achieves similar optimization using internal caching mechanisms

---

# Conclusion

- SQLite is good for:
  - Small apps
  - Local storage
  - Low concurrency

- PostgreSQL is for:
  - Large applications
  - Multi-user systems
  - High performance needs


---

# Bonus Insight

Enabling mmap resulted in ~75% faster query execution (209 ms → 53 ms), showing the efficiency of memory-mapped I/O.

In PostgreSQL, repeated queries became faster (~16 ms → ~5 ms) due to caching (shared buffers and OS page cache), reducing disk access.