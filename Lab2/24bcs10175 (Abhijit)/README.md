# Lab 2 - SQLite3 and PostgreSQL Comparison

**Name:** Abhijit P
**Roll No:** 24bcs10175

## Objective

The objective of this assignment was to explore SQLite3 and PostgreSQL by performing basic database operations and comparing their behavior.

The assignment includes:

* database creation
* table creation
* inserting sample data
* running queries
* checking page size and page count
* checking mmap_size
* measuring query execution time
* comparing SQLite3 and PostgreSQL

---

## SQLite3 Work

SQLite3 was used to create a local file-based database named `football_clubs`.

### Table Created

### football_clubs

| id | name                 | location | ucl |
| -- | -------------------- | -------- | --- |
| 1  | Barcelona FC         | Spain    | 5   |
| 2  | Real Madrid FC       | Spain    | 15  |
| 3  | FC Bayern Munich     | Germany  | 6   |
| 4  | Liverpool FC         | England  | 6   |
| 5  | Manchester United FC | England  | 3   |
| 6  | Manchester City FC   | England  | 1   |
| 7  | Arsenal FC           | England  | 0   |

---

## SQLite3 Storage Analysis

### Database File Size

```bash
-rwxrwxrwx 1 adx adx 12K May 9 05:05 football_clubs
```

### Page Size

Observed page size:

```text
4096 bytes
```

### Page Count

Observed page count:

```text
3
```

Page count increased after inserting more data into the database.

### mmap_size

Default mmap_size:

```text
0
```

Updated mmap_size:

```text
268435456
```

---

## SQLite Internal Architecture

SQLite is an embedded database engine that stores the entire database inside a single file. Internally, SQLite uses a Pager subsystem to manage communication between memory and disk.

```text
Application
    ↓
SQLite Engine
    ↓
Pager
    ↓
Page Cache
    ↓
Database File
```

### Pager

The Pager is responsible for:

* Reading pages from disk
* Writing modified pages back to disk
* Managing transactions
* Handling file locking
* Maintaining database consistency

### Pages

SQLite stores all tables, indexes, and metadata inside fixed-size pages.

For this database:

```text
Page Size = 4096 bytes
```

Instead of reading individual rows, SQLite reads and writes complete pages.

### Page Cache

Frequently accessed pages are stored in memory inside the page cache.

Benefits:

* Reduced disk I/O
* Faster query execution
* Better performance for repeated access

---

## PRAGMA Commands Used

SQLite provides PRAGMA commands to inspect and configure internal database settings.

### PRAGMA page_size

```sql
PRAGMA page_size;
```

Returns the size of a single database page.

Observed value:

```text
4096 bytes
```

### PRAGMA page_count

```sql
PRAGMA page_count;
```

Returns the total number of pages allocated in the database file.

Observed value:

```text
3
```

Estimated database size:

```text
4096 × 3 = 12288 bytes
```

### PRAGMA mmap_size

```sql
PRAGMA mmap_size;
```

Returns or configures the amount of the database file that can be memory mapped by the operating system.

Observed values:

```text
Default: 0
Updated: 268435456
```

Memory mapping allows SQLite to access database pages directly through memory, reducing the need for repeated file read operations.

---

## Memory-Mapped I/O (mmap)

Memory mapping allows the operating system to map a database file directly into a process's virtual memory space.

### Without mmap

```text
Database File
    ↓
read()
    ↓
SQLite Buffer
    ↓
Application
```

### With mmap

```text
Database File
    ↓
Memory Mapping
    ↓
Application
```

Advantages:

* Fewer system calls
* Reduced memory copying
* Faster read performance
* Better utilization of operating system caching

For this small dataset, the performance difference was negligible because the database size was very small.

---

## SQLite3 Query Timing

### Query Used

```sql
SELECT * FROM football_clubs;
```

### Without mmap

```text
real    0m0.009s
user    0m0.003s
sys     0m0.000s
```

### With mmap

```text
real    0m0.009s
user    0m0.003s
sys     0m0.000s
```

For this small dataset, enabling mmap did not produce a noticeable performance improvement.

---

## PostgreSQL Work

PostgreSQL was installed and tested using the same football_clubs dataset.

### PostgreSQL Query Timing

```text
real    0m0.039s
user    0m0.006s
sys     0m0.004s
```

---

## SQLite3 vs PostgreSQL Comparison

| Feature       | SQLite3                           | PostgreSQL                  |
| ------------- | --------------------------------- | --------------------------- |
| Database Type | File-based database               | Server-based database       |
| Setup         | Lightweight and simple            | Requires PostgreSQL server  |
| Performance   | Fast for small/local applications | Better for scalable systems |
| Concurrency   | Limited                           | Better concurrency support  |
| Usage         | Embedded/local applications       | Enterprise applications     |

---

## Environment Used

The assignment was tested in a Linux/WSL environment using bash.

PostgreSQL setup used Linux-specific authentication with:

```bash
sudo -u postgres
```

---

## Conclusion

SQLite3 is lightweight, simple, and useful for local applications and testing.

PostgreSQL is more suitable for scalable applications and systems requiring better concurrency and server-based architecture.

The assignment helped in understanding:

* database storage concepts
* page size and page count
* PRAGMA commands
* memory-mapped I/O (mmap)
* query timing
* SQLite internals
* Pager and page cache architecture
* comparison between SQLite3 and PostgreSQL
