# scaler-Adv-DBMS


# Advanced DBMS — Lab 1:

**Roll Number:** *24BCS10189*
**Name:** *Ankit Kumar*

## Objective

To explore SQLite3 and PostgreSQL storage behavior and compare:

- Page size
- Page count
- Query performance
- Impact of `mmap`

## 1. SQLite3 Exploration

### Environment

- OS: Windows
- SQLite version: `3.53.0`
- Sample database: `sample.db`
- Table used: `users`
- Number of rows inserted: `100000`

### Commands Used

```bash
ls -lh
```

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
SELECT * FROM users;
```

Windows commands used for this experiment:

```powershell
sqlite3 D:\ankit\adbms\_lab_tmp\sample.db "PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF; CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, email TEXT); WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM cnt WHERE x < 100000) INSERT INTO users(name, email) SELECT 'User' || x, 'user' || x || '@example.com' FROM cnt;"
```

```powershell
sqlite3 D:\ankit\adbms\_lab_tmp\sample.db "PRAGMA page_size; PRAGMA page_count; PRAGMA mmap_size; SELECT COUNT(*) FROM users;"
```

```powershell
$t = Measure-Command { sqlite3 D:\ankit\adbms\_lab_tmp\sample.db "PRAGMA mmap_size=0; SELECT * FROM users;" | Out-Null }
```

```powershell
$t = Measure-Command { sqlite3 D:\ankit\adbms\_lab_tmp\sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" | Out-Null }
```

### Observations

| Metric | Observation |
| --- | --- |
| Database file size | `3.82 MB` (`4001792` bytes) |
| Page size | `4096` bytes |
| Page count | `977` |
| Default `mmap_size` | `0` |
| Updated `mmap_size` | `268435456` bytes (`256 MB`) |
| Total rows in `users` | `100000` |

### File Size Observation

```bash
total 3.9M
-rw-r--r-- 1 harsh harsh 3.9M sample.db
```

### Query Timing

Query tested:

```sql
SELECT * FROM users;
```

#### Without mmap (`PRAGMA mmap_size=0`)

- Run 1: `871.71 ms`
- Run 2: `892.91 ms`
- Run 3: `856.69 ms`
- Approx average: `873.77 ms`

#### With mmap (`PRAGMA mmap_size=268435456`)

- Run 1: `863.40 ms`
- Run 2: `859.20 ms`
- Run 3: `850.20 ms`
- Approx average: `857.60 ms`

### SQLite Analysis

- SQLite used a default page size of `4096` bytes.
- The database occupied `977` pages for `100000` rows.
- The file size and page count were consistent with a compact single-file embedded database.
- Increasing `mmap_size` worked successfully.
- Query execution with mmap was slightly faster, but the difference was small for this database size.
- For a relatively small local database, mmap did not create a dramatic performance improvement.

## 2. PostgreSQL Setup

### Commands Used

```sql
SHOW block_size;
```

```sql
SELECT relpages, reltuples
FROM pg_class
WHERE relname = 'users';
```

```sql
SELECT * FROM users;
```

For query timing:

```powershell
$t = Measure-Command { psql -d <database_name> -c "SELECT * FROM users;" | Out-Null }
```

### Observation Table

| Metric | Observation |
| --- | --- |
| Page size / block size | `Not recorded` |
| Page count | `Not recorded` |
| Query execution time | `Not recorded` |

## 3. SQLite3 vs PostgreSQL Comparison

| Feature | SQLite3 | PostgreSQL |
| --- | --- | --- |
| Storage model | Single database file | Client-server database system |
| Page size | `4096` bytes | `Not recorded` |
| Page count | `977` | `Not recorded` |
| Query performance | ~`857-874 ms` for full table scan | `Not recorded` |
| mmap impact | Slight improvement observed | `Not tested` |
| Setup complexity | Very simple | Higher than SQLite |

## 4. Analysis

- In SQLite, the database stayed compact and easy to inspect because everything was stored in a single file.
- The page size was `4096` bytes and the page count was `977`, which matched the final database size.
- Changing `mmap_size` did not produce a major difference in execution time for this dataset, although the mmap runs were slightly faster on average.
- This suggests that for a small to medium local database, mmap may help a little, but the improvement is not very large in a simple full table scan.
- The PostgreSQL commands required for the same comparison are listed above, but the corresponding observations were not recorded in this report.

## Conclusion

From the SQLite experiment, I was able to check file size, page size, page count, and the effect of changing `mmap_size` on query timing. The main difference I observed was that mmap gave only a small improvement for this particular database and query. The PostgreSQL section has been included in the same format for comparison, with fields left unrecorded where observations were not available.