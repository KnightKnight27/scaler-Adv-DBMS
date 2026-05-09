## Student Details

**Name:** Aparna Singha  
**Roll No:** 24BCS10353  
**Course:** Advanced DBMS  
**Submission:** Lab 2

---

# Lab 2: SQLite3 and PostgreSQL Comparison

## 1. What this lab is about

In this lab, I compared SQLite3 and PostgreSQL using a small `users` table. I mainly checked how both databases store data and how some basic storage-related values are different.

The things I focused on were:

- database/file size
- page size or block size
- page count
- query timing
- SQLite `mmap_size`
- basic difference between embedded and server-based database systems

SQLite3 stores the database in one local `.db` file. PostgreSQL works as a database server and keeps more internal metadata, background processes, catalogs, and transaction-related data.

---

## 2. SQLite3 experiment

### 2.1 Starting SQLite3

```bash
sqlite3 sample.db
```

### 2.2 Commands used

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
);

INSERT INTO users (name, email) VALUES
('Aarav', 'aarav@example.com'),
('Diya', 'diya@example.com'),
('Rohan', 'rohan@example.com'),
('Meera', 'meera@example.com');

.tables

PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;

.timer ON

PRAGMA mmap_size = 0;
SELECT * FROM users;

PRAGMA mmap_size = 268435456;
PRAGMA mmap_size;
SELECT * FROM users;
```

### 2.3 Checking the SQLite database file size

Linux/macOS:

```bash
ls -lh sample.db
```

Windows PowerShell:

```powershell
dir sample.db
```

---

## 3. SQLite3 observations

| Parameter | Value I observed |
|---|---:|
| SQLite Version | 3.53.1 |
| Database File | `sample.db` |
| Database File Size | 8192 bytes |
| Page Size | 4096 bytes |
| Page Count | 2 |
| mmap Size Before Change | 0 |
| mmap Size After Change | 268435456 |
| Query Time Without mmap | 0.015015 seconds |
| Query Time With mmap | 0.008886 seconds |

### My SQLite3 notes

The `users` table had only four rows, so the database file stayed very small. SQLite stored the complete database inside `sample.db`. The page size was 4096 bytes and the page count was 2, so the total file size matched two pages.

At first, `mmap_size` was `0`, so memory-mapped I/O was not being used. After setting it to `268435456`, the query time became slightly lower in my run. Since the table was very small, this difference should not be treated as a big performance result. It just shows that `mmap_size` can affect how SQLite reads the database file.

---

## 4. PostgreSQL experiment

### 4.1 Starting PostgreSQL

On my Windows setup, I used PostgreSQL from its installed path:

```powershell
& "C:\Program Files\PostgreSQL\17\bin\psql.exe" -U postgres -h 127.0.0.1 -p 5433
```

On Linux/macOS, the command may look like this:

```bash
psql -U postgres -h 127.0.0.1 -p 5432
```

### 4.2 Commands used

```sql
DROP DATABASE IF EXISTS lab2db;
CREATE DATABASE lab2db;

\c lab2db

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    email VARCHAR(100)
);

INSERT INTO users (name, email) VALUES
('Aarav', 'aarav@example.com'),
('Diya', 'diya@example.com'),
('Rohan', 'rohan@example.com'),
('Meera', 'meera@example.com');

SELECT current_setting('block_size');
SELECT pg_database_size('lab2db');

\timing
SELECT * FROM users;
```

---

## 5. PostgreSQL observations

| Parameter | Value I observed |
|---|---:|
| PostgreSQL Version | 17.9 |
| Database Name | `lab2db` |
| Port Used | 5433 |
| Block Size | 8192 bytes |
| Database Size | 8189619 bytes |
| Query Execution Time | 3.566 ms |

### My PostgreSQL notes

I created the same type of `users` table in PostgreSQL. The block size came out as 8192 bytes, which is larger than SQLite's 4096-byte page size.

The PostgreSQL database size was much bigger than the SQLite file size, even though the user table was very small. This makes sense because PostgreSQL is not just storing my four rows. It also has system catalogs, transaction information, metadata, WAL-related behavior, and server-managed storage structures.

---

## 6. SQLite3 vs PostgreSQL comparison

| Point | SQLite3 | PostgreSQL |
|---|---|---|
| Type | Embedded database | Server-based relational database |
| Setup | Very simple, no separate server needed | Needs PostgreSQL server setup |
| Storage | One `.db` file | PostgreSQL-managed files and folders |
| Size in my experiment | 8192 bytes | 8189619 bytes |
| Page / block size | 4096 bytes | 8192 bytes |
| Page count | 2 pages | Not measured in the same direct way as SQLite |
| Query timing | 0.015015 sec without mmap, 0.008886 sec with mmap | 3.566 ms |
| mmap | Controlled using `PRAGMA mmap_size` | No SQLite-style `mmap_size` setting |
| Better use case | Small apps, local storage, simple prototypes | Multi-user apps, production systems, larger workloads |

---

## 7. Storage engine points I connected with this lab

### Pages and blocks

Both SQLite and PostgreSQL divide database data into fixed-size units. SQLite calls them pages, and PostgreSQL commonly talks about blocks/pages. In my experiment, SQLite used 4096-byte pages and PostgreSQL used 8192-byte blocks.

This is important because databases do not usually read or write one row as a random standalone object. They manage data page by page.

### SQLite storage idea

SQLite keeps the whole database in a single file. Inside that file, data is organized into pages. Tables and indexes are stored using B-tree structures. Records inside those pages are variable-length, so the database has to keep track of where each record starts and ends.

### PostgreSQL heap storage idea

PostgreSQL stores table rows in heap files. A heap page has a page header, line pointers, free space, and tuple data. The line pointers help PostgreSQL find rows inside the page.

### Free space and tuple deletion

PostgreSQL does not always remove old row versions immediately. When rows are updated or deleted, old tuple versions can remain for some time because of MVCC. Later, `VACUUM` can clean dead tuples and make that space reusable.

This part helped me connect the experiment with storage engine topics like heap files, free space management, variable-length tuples, and tuple deletion.

---

## 8. Analysis

SQLite3 felt much simpler to use because it does not need a separate server. The database is just one file, so it is easy to create, move, and inspect. For small local applications, this is a big advantage.

PostgreSQL is heavier, but it gives more features for real applications. It supports better concurrency, permissions, transactions, indexing options, and server-side management. The extra storage overhead is expected because it is built for reliability and multi-user workloads.

In this experiment, the dataset was very small, so query timings should not be over-interpreted. The main useful observation was the storage difference: SQLite stayed tiny, while PostgreSQL had more overhead because of its full database-server architecture.

---

## 9. Lab 2 conclusion

From this lab, I understood that SQLite3 and PostgreSQL are both relational databases, but their design goals are different.

SQLite3 is lightweight and file-based. PostgreSQL is server-based and more suitable for larger applications where concurrency, reliability, and management features matter.

The page size, block size, mmap setting, and database-size difference helped me connect DBMS architecture with storage engine internals.

---

# Final overall conclusion

Lab 1 helped me understand how file I/O works at the operating-system level using system calls and file descriptors. Lab 2 helped me see how databases use similar storage ideas internally through pages, blocks, caching, and file-based storage.

Together, both labs made the connection between OS-level storage and database storage engines much clearer.
