# Lab 2 – Comparative Study of SQLite3 and PostgreSQL

**Name:** Amitabh Panda
**Roll No:** 24BCS10104

## Aim

The purpose of this experiment was to gain practical experience with both SQLite3 and PostgreSQL by creating databases, inserting records, executing queries, analyzing storage characteristics, and comparing performance.

The tasks performed included:

* Creating databases
* Creating tables
* Inserting sample records
* Executing SQL queries
* Inspecting page size and page count
* Configuring memory-mapped I/O
* Measuring query execution time
* Comparing SQLite3 and PostgreSQL

---

# SQLite3 Implementation

SQLite3 was used to create a lightweight file-based database called **tech_companies.db**.

## Table Structure

### tech_companies

| ID | Company Name | Country     | Founded |
| -- | ------------ | ----------- | ------- |
| 1  | Apple Inc.   | USA         | 1976    |
| 2  | Microsoft    | USA         | 1975    |
| 3  | Google       | USA         | 1998    |
| 4  | Samsung      | South Korea | 1938    |
| 5  | IBM          | USA         | 1911    |
| 6  | Intel        | USA         | 1968    |
| 7  | Sony         | Japan       | 1946    |

---

# Storage Characteristics of SQLite3

## Database File Size

```bash
-rwxrwxrwx 1 user user 12K May 9 05:05 tech_companies.db
```

The database occupied approximately 12 KB on disk after inserting the records.

---

## Page Size

Using SQLite PRAGMA commands, the page size was found to be:

```text
4096 bytes
```

This indicates that SQLite manages storage in blocks of 4 KB.

---

## Page Count

The total number of allocated pages was:

```text
3
```

Additional inserts would increase this value as the database grows.

---

## Memory Mapping Size

Initial value:

```text
0
```

Updated value:

```text
268435456
```

This configuration allows SQLite to access part of the database through memory mapping.

---

# SQLite Internal Design

SQLite is an embedded relational database that stores all information inside a single disk file. It relies on several internal components to efficiently manage data.

```text
Application
     ↓
SQLite Engine
     ↓
Pager Layer
     ↓
Page Cache
     ↓
Database File
```

---

## Pager Layer

The Pager is responsible for communication between SQLite and the filesystem.

Its duties include:

* Reading data pages
* Writing modified pages
* Transaction handling
* File locking
* Ensuring ACID compliance

---

## Database Pages

SQLite organizes information into fixed-size pages.

For this experiment:

```text
Page Size = 4096 bytes
```

Tables, indexes, and metadata are all stored inside these pages.

Rather than accessing individual rows directly, SQLite retrieves complete pages into memory.

---

## Page Cache

Frequently accessed pages are stored in RAM.

Advantages include:

* Reduced disk access
* Improved query response time
* Better performance during repeated operations

---

# PRAGMA Commands

SQLite provides special commands known as PRAGMA statements for inspecting and configuring internal settings.

---

## PRAGMA page_size

```sql
PRAGMA page_size;
```

Returns the size of a database page.

Result:

```text
4096 bytes
```

---

## PRAGMA page_count

```sql
PRAGMA page_count;
```

Returns the number of pages allocated.

Result:

```text
3
```

Approximate database size:

```text
4096 × 3 = 12288 bytes
```

---

## PRAGMA mmap_size

```sql
PRAGMA mmap_size;
```

Used to view or modify the memory-mapped region size.

Observed values:

```text
Default: 0
Modified: 268435456
```

Memory mapping enables SQLite to access file contents directly through virtual memory rather than repeatedly issuing file read requests.

---

# Memory-Mapped I/O

Memory mapping provides a mechanism for the operating system to map a file directly into a process's address space.

### Traditional Access

```text
Database File
      ↓
   read()
      ↓
SQLite Buffer
      ↓
 Application
```

### Memory-Mapped Access

```text
Database File
      ↓
 Memory Mapping
      ↓
 Application
```

Benefits:

* Reduced system call overhead
* Less data copying
* Faster read access
* Better utilization of OS caching

For a small database such as this one, the performance impact was minimal.

---

# SQLite Query Performance

### Query Executed

```sql
SELECT * FROM tech_companies;
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

The execution time remained nearly identical due to the small dataset size.

---

# PostgreSQL Implementation

The same dataset was recreated in PostgreSQL and queried for comparison.

## Query Timing

```text
real    0m0.039s
user    0m0.006s
sys     0m0.004s
```

PostgreSQL introduced additional overhead because it operates as a separate database server process.

---

# SQLite3 vs PostgreSQL

| Feature       | SQLite3                          | PostgreSQL                          |
| ------------- | -------------------------------- | ----------------------------------- |
| Architecture  | Embedded database                | Client-server database              |
| Installation  | Minimal setup                    | Requires server installation        |
| Storage       | Single file                      | Managed database cluster            |
| Performance   | Excellent for local applications | Optimized for large-scale workloads |
| Concurrency   | Basic locking mechanisms         | Advanced multi-user support         |
| Typical Usage | Mobile, desktop, testing         | Enterprise and production systems   |

---

# Environment

The experiment was conducted in a Linux/WSL environment using Bash.

PostgreSQL access was performed through:

```bash
sudo -u postgres
```

which uses Linux user authentication for administrative access.

---

# Summary

This exercise provided hands-on exposure to both SQLite3 and PostgreSQL.

SQLite3 proved to be lightweight, portable, and easy to use for local applications and small projects. PostgreSQL, while requiring more setup, offers stronger concurrency control and scalability for production environments.

Key concepts explored during the lab included:

* Database creation and management
* Storage organization
* Page size and page count analysis
* PRAGMA commands
* Memory-mapped I/O
* Query performance measurement
* SQLite internals
* Pager and cache mechanisms
* Comparison of embedded and server-based databases
