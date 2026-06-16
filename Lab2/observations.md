# Lab Session 2: SQLite3 Internals

## Objective

To understand SQLite3 storage internals using PRAGMA commands and compare SQLite3 architecture with PostgreSQL.

---

## Environment Details

**Operating System:** macOS

**SQLite Version:**

```bash
sqlite3 --version
```

Output:

```text
3.51.0 2025-06-12 13:14:41 f0ca7bba1c5e232e5d279fad6338121ab55af0c8c68c84cdfb18ba5114dcaapl (64-bit)
```

---

## PRAGMA Commands

### 1. Page Size

```sql
PRAGMA page_size;
```

Output:

```text
4096
```

Observation:

* SQLite divides the database file into fixed-size pages.
* The page size is 4096 bytes.
* This matches the typical operating system memory page size.

---

### 2. Page Count

```sql
PRAGMA page_count;
```

Output:

```text
0
```

Observation:

* Page count indicates the number of pages allocated in the database file.
* Since the database is newly created and contains no data, the page count is minimal.
* Total database size can be estimated as:

```text
Database Size = Page Size × Page Count
```

---

### 3. Memory-Mapped I/O (mmap)

Check current mmap size:

```sql
PRAGMA mmap_size;
```

Output:

```text
0
```

Observation:

* Memory-mapped I/O is disabled by default.

Enable mmap:

```sql
PRAGMA mmap_size = 268435456;
```

Verify:

```sql
PRAGMA mmap_size;
```

Output:

```text
268435456
```

Observation:

* SQLite now maps up to 256 MB of the database file into memory.
* Memory-mapped I/O reduces the number of read() system calls.
* Sequential reads become faster because the operating system manages page loading directly.

---

### 4. Journal Mode

```sql
PRAGMA journal_mode;
```

Output:

```text
delete
```

Observation:

* DELETE is the default journaling mode.
* Transaction logs are stored in a rollback journal and deleted after the transaction completes.
* Other available modes include WAL, MEMORY, OFF, and TRUNCATE.

---

### 5. Cache Size

```sql
PRAGMA cache_size;
```

Output:

```text
2000
```

Observation:

* SQLite keeps approximately 2000 pages in its cache.
* A larger cache can improve query performance by reducing disk accesses.

---

### 6. Integrity Check

```sql
PRAGMA integrity_check;
```

Output:

```text
ok
```

Observation:

* The database structure is valid.
* No corruption or page inconsistencies were detected.

---

### 7. Database List

```sql
PRAGMA database_list;
```

Output:

```text
0|main|/Users/anushka/Desktop/Advanced-DMBS-02/scaler-Adv-DBMS/Lab2/students.db
1|temp|
```

Observation:

* The main database is students.db.
* SQLite automatically creates a temporary database when required.

---

## SQLite3 Architecture

SQLite is an embedded database engine that runs inside the application's process.

Architecture:

```text
Application
      |
      v
SQLite Library
      |
      v
students.db
```

Characteristics:

* No separate database server process.
* No TCP/IP communication.
* No authentication handshake.
* Database access occurs through direct library function calls.
* Uses operating system file I/O for storage.

---

## Verifying SQLite is a Library

### Check for Running SQLite Server

Command:

```bash
ps aux | grep sqlite
```

Output:

```text
anushka 73368 0.0 0.0 ... grep sqlite
```

Observation:

* No dedicated SQLite server process exists.
* Only the grep command itself appears.
* This confirms SQLite operates as an embedded library rather than a standalone server.

---

### Dynamic Library Inspection

On macOS:

```bash
otool -L $(which sqlite3)
```

Output:

```text
/usr/bin/sqlite3:
    /usr/lib/libz.1.dylib
    /usr/lib/libncurses.5.4.dylib
    /usr/lib/libedit.3.dylib
    /usr/lib/libSystem.B.dylib
```

Observation:

* The sqlite3 executable is dynamically linked with system libraries.
* macOS does not provide the Linux ldd command.
* Therefore, otool -L was used to inspect library dependencies.

---

# PostgreSQL vs SQLite3

| Feature        | SQLite3                         | PostgreSQL                         |
| -------------- | ------------------------------- | ---------------------------------- |
| Architecture   | Embedded Library                | Client-Server                      |
| Process Model  | Runs inside application process | Separate postgres daemon           |
| Communication  | Function calls and file I/O     | TCP/IP or Unix sockets             |
| Storage        | Single .db file                 | Data directory with multiple files |
| Authentication | File permissions                | Roles, passwords, SSL              |
| Concurrency    | One writer at a time            | MVCC supports multiple writers     |
| Deployment     | Zero configuration              | Requires server setup              |
| Best Use Case  | Embedded and local applications | Multi-user production systems      |

---

## mmap and Performance

SQLite can use:

```c
mmap()
```

to map database pages directly into the process address space.

Advantages:

* Fewer read() system calls
* Faster sequential reads
* Better utilization of the operating system page cache
* Reduced overhead during database access

PostgreSQL primarily uses:

```text
shared_buffers
```

for memory management instead of relying on mmap for normal database access.

---

## Conclusion

SQLite is a lightweight embedded database that runs as a library within an application process. It stores data in a single database file and supports memory-mapped I/O for improved read performance. SQLite is ideal for local applications, mobile apps, testing environments, and low-concurrency workloads.

PostgreSQL follows a client-server architecture and uses MVCC to support high concurrency, advanced transaction isolation, authentication, and enterprise-scale workloads. The choice between SQLite and PostgreSQL depends on the application's concurrency requirements, deployment model, and scalability needs.
