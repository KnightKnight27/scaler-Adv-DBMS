 
## SQLite3 vs PostgreSQL: Page Size, Page Count, mmap Behavior, and Process Observation

## 1. Objective

The purpose of this assignment is to compare SQLite3 and PostgreSQL from an internal database-engine perspective.  
The comparison focuses on:

- default page size / block size,
- page count and storage layout,
- memory-mapped I/O (mmap) behavior,
- process architecture,
- and overall design differences between an embedded database and a client-server DBMS.

---

## 2. SQLite3 Experiment

### Database Creation

Created a sample table in SQLite3.

```sql
CREATE TABLE movies (
    id INTEGER PRIMARY KEY,
    name TEXT
);

INSERT INTO movies (name)
VALUES ('Godfather'), ('Moneyball'), ('The Big Short');
```

### File Size Observation

The SQLite database was stored as a single `.db` file.  
This is one of the clearest signs of SQLite’s embedded architecture.

Observations:
- the database existed as one file
- the file size remained small for a tiny table
- no separate server storage layout was required

### Page Size and Page Count

```sql
PRAGMA page_size;
PRAGMA page_count;
```

Observed values:

| Property | Value |
|---|---:|
| Page size | 4096 bytes |
| Page count | 2 |

This means the database occupied two 4 KB pages in total.  
For SQLite, the entire database file is organized into fixed-size pages, and the total database size can be understood as:

```text
page_size × page_count
```
### mmap Experiment

SQLite allows memory-mapped I/O through `PRAGMA mmap_size`.

```sql
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
```

Observed values:

| Setting | Value |
|---|---:|
| Default mmap size | 0 |
| Enabled mmap size | 268435456 bytes |

- By default, SQLite does not use memory-mapped I/O.
- After setting `mmap_size`, SQLite can map database pages directly into virtual memory which can reduce I/O overhead for read-heavy workloads.

### Query Timing Observation

A simple `SELECT` query was executed before and after enabling mmap.

```sql
SELECT * FROM users;
```

Observed result:

- query execution was extremely fast in both cases
- the improvement from mmap was small because the dataset was tiny

### Process Observation

```bash
ps aux | grep sqlite
```

Observation:

- SQLite did not appear as a long-running standalone database server.
- It behaves like an embedded library inside the application process.
- This makes SQLite easy to use but less suitable for high-concurrency server workloads.

---

## 3. PostgreSQL Experiment

### Database and Table Creation

Created a PostgreSQL database and added a sample table.

```sql
CREATE DATABASE testdb;

CREATE TABLE movies (
    id SERIAL PRIMARY KEY,
    name TEXT
);

INSERT INTO movies (name)
VALUES ('Godfather'), ('Moneyball'), ('The Big Short');
```

### Storage Observation

PostgreSQL does not store everything in one visible database file the way SQLite does.  
Instead, it manages data inside its data directory and relation files.
This difference is important because PostgreSQL is designed as a server system, not as a single-file embedded engine.

### Block Size

The PostgreSQL block size was checked using:

```sql
SHOW block_size;
```

Observed value:

| Property | Value |
|---|---:|
| Block size | 8192 bytes |

PostgreSQL uses 8 KB blocks by default. This is larger than SQLite’s 4 KB pages.

### Page Count / Relation Size

Unlike SQLite, PostgreSQL does not report one global page count for the whole database in the same simple way. Instead, page usage is usually inspected at the table or relation level.

```sql
SELECT
    pg_relation_size('users') / current_setting('block_size')::int
    AS approx_page_count;
```

### Query Timing

```sql
SELECT * FROM users;
```

Observed behavior:

- the query was also very fast
- repeated execution became slightly faster because of caching

### Process Observation

The following command was used:

```bash
ps aux | grep postgres
```

Observation:
- PostgreSQL appeared as multiple background processes.
- This confirms its client-server architecture.
- This is a major difference from SQLite, which does not normally run as a separate server.

---

## 4. Comparison of SQLite3 and PostgreSQL

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | Embedded library | Client-server DBMS |
| Storage style | Single database file | Data directory with many internal files |
| Default page/block size | 4096 bytes | 8192 bytes |
| Page count view | Global `PRAGMA page_count` | Relation-level size/page estimation |
| mmap support | Directly tunable with `PRAGMA mmap_size` | No user-facing equivalent like SQLite |
| Process model | Runs inside application process | Multiple server/background processes |
| Setup complexity | Very simple | More complex |
| Best suited for | Local apps, embedded use, small projects | Multi-user systems, production servers |

---
## 5. Conclusion

SQLite3 is compact, file-based, and easy to inspect. It is ideal for embedded applications and small-scale local storage. Its page structure is simple, and `PRAGMA mmap_size` gives direct control over memory-mapped I/O.

PostgreSQL is a more advanced database system built for multi-user and production environments. It uses larger blocks, server-managed background processes, and internal storage mechanisms that are more complex but also more powerful.

In short, SQLite is better when simplicity matters, while PostgreSQL is better when scalability, concurrency, and administrative control are required.

---
