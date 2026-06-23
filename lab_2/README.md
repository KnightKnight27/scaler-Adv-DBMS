# Lab 2: SQLite3 Internals & Architecture Comparison

**Student:** Pulasari Jai  
**Roll No:** 24BCS10656  
**Date:** June 23, 2026

---

## Overview

This lab explores SQLite3's internal storage mechanisms through PRAGMA commands and provides a comprehensive architectural comparison between PostgreSQL (client-server) and SQLite3 (in-process library). We examine page management, memory-mapped I/O, journaling modes, and concurrency models.

---

## Objectives

1. ✅ Install and verify SQLite3
2. ✅ Explore storage internals via PRAGMA commands
3. ✅ Test memory-mapped I/O (mmap) performance
4. ✅ Implement C++ program using SQLite3 library
5. ✅ Document comprehensive PostgreSQL vs SQLite3 comparison

---

## Directory Structure

```
lab_2/
├── README.md                       # This file
├── setup_database.sql              # Database creation script
├── pragma_exploration.sql          # PRAGMA command exploration
├── pragma_results.txt              # Output of PRAGMA commands
├── test_mmap.sql                   # mmap performance test
├── sqlite_demo.cpp                 # C++ SQLite3 demo
├── compile.sh                      # Compilation script
├── architecture_comparison.md      # Detailed comparison document
├── students.db                     # SQLite database file
└── sqlite_demo                     # Compiled binary
```

---

## SQLite3 Installation

### Verification

```bash
$ sqlite3 --version
3.51.0 2025-06-12 13:14:41
```

✅ SQLite3 is already installed (version 3.51.0)


---

## Implementation

### 1. Database Setup

Created a sample database with three tables:
- **students:** 8 student records with id, name, age, gpa, major
- **courses:** 5 course records
- **enrollments:** 6 enrollment records (many-to-many relationship)

```bash
$ sqlite3 students.db < setup_database.sql
Database setup complete!
8
5
6
```

✅ Database created successfully with sample data

### 2. PRAGMA Exploration

Explored 18 different PRAGMA commands to understand SQLite internals:

```bash
$ sqlite3 students.db < pragma_exploration.sql > pragma_results.txt
```

**Key findings:**

| PRAGMA | Value | Meaning |
|--------|-------|---------|
| page_size | 4096 | Each page is 4KB (matches OS page size) |
| page_count | 5 | Database has 5 pages |
| File size | 20 KB | page_size × page_count = 4096 × 5 |
| mmap_size | 0 | mmap disabled by default |
| journal_mode | delete | Traditional rollback journal |
| cache_size | 2000 | 2000 pages in memory cache |
| synchronous | 2 | FULL (safest, slowest) |
| encoding | UTF-8 | Unicode support |
| auto_vacuum | 0 | NONE (manual VACUUM required) |


### 3. Memory-Mapped I/O Testing

Tested performance with and without mmap:

```bash
$ sqlite3 students.db < test_mmap.sql
```

**Results:**

| Configuration | Query Time (μs) | Speedup |
|---------------|-----------------|---------|
| mmap OFF | user 92 + sys 57 = 149 μs | baseline |
| mmap ON (256 MB) | user 12 + sys 9 = 21 μs | **7.1x faster** |

✅ mmap significantly reduces system call overhead

**How mmap works:**
```
Without mmap:
  read() syscall → kernel buffer → user buffer (2 copies)

With mmap:
  File pages mapped directly into process address space (zero-copy)
```

### 4. C++ SQLite3 Demo

Implemented a C++ program that:
- Opens SQLite database using `libsqlite3`
- Executes PRAGMA commands
- Runs SELECT queries with callback function
- Demonstrates in-process library architecture

```bash
$ ./compile.sh
Compiling sqlite_demo.cpp...
✓ Compilation successful!

$ ./sqlite_demo
=== SQLite3 C++ Demo ===
SQLite Version: 3.51.0

✓ Database opened successfully
...
✓ Database closed successfully
```

**Key observation:** No separate server process! The database runs **inside** the application process.

Verification:
```bash
$ ps aux | grep sqlite
# No sqlite server process found!
```


---

## Testing Results

### ✅ PRAGMA Exploration Test
```bash
$ sqlite3 students.db < pragma_exploration.sql > pragma_results.txt
$ wc -l pragma_results.txt
73 pragma_results.txt
```
**Status:** PASS - All 18 PRAGMA commands executed successfully

### ✅ mmap Performance Test
```
Test 1 (mmap OFF): 149 μs
Test 2 (mmap ON):  21 μs
Speedup: 7.1x
```
**Status:** PASS - Significant performance improvement observed

### ✅ C++ Compilation Test
```bash
$ ./compile.sh
Compiling sqlite_demo.cpp...
✓ Compilation successful!
```
**Status:** PASS - No errors or warnings

### ✅ C++ Execution Test
```bash
$ ./sqlite_demo
SQLite Version: 3.51.0
✓ Database opened successfully
...
4. Students with GPA > 3.5:
name = Eve Brown
gpa = 3.9
---
✓ Database closed successfully
```
**Status:** PASS - All queries executed correctly

---

## Key Concepts Demonstrated

### 1. SQLite is a Library, Not a Server

```
PostgreSQL:
  App → network → postgres server → disk

SQLite3:
  App → libsqlite3.so (in-process) → disk
```

**Implications:**
- ✅ No server setup/maintenance
- ✅ Zero network latency
- ✅ Single file = entire database
- ❌ No concurrent writers
- ❌ No network access
- ❌ No user authentication


### 2. Page-Based Storage

```
students.db structure:
┌─────────────────────┐
│ Page 0: Header       │ ← Database metadata
├─────────────────────┤
│ Page 1: Schema       │ ← sqlite_master table
├─────────────────────┤
│ Pages 2-4: Data      │ ← Student/course/enrollment data
└─────────────────────┘

File size = page_size (4096) × page_count (5) = 20,480 bytes
```

### 3. mmap Optimization

```
Traditional I/O:
  Disk → Kernel buffer → User buffer (2 copies, 2 context switches)

mmap:
  Disk ← mapped → Process address space (zero-copy, page faults)
```

**When to use mmap:**
- ✅ Read-heavy workloads
- ✅ Large sequential scans
- ✅ Embedded systems (limited memory for buffer pool)
- ❌ Write-heavy workloads (writes still use traditional I/O)

### 4. Journal Modes

| Mode | Concurrency | Use Case |
|------|-------------|----------|
| DELETE | 1 writer, N readers blocked | Legacy, simple |
| TRUNCATE | 1 writer, N readers blocked | Slightly faster |
| PERSIST | 1 writer, N readers blocked | Avoid journal delete |
| WAL | 1 writer, N readers OK | **Recommended for most apps** |
| MEMORY | Fast, not durable | Testing only |
| OFF | Unsafe | Never use in production |

**Switch to WAL mode:**
```sql
PRAGMA journal_mode = WAL;
```

Benefits:
- Readers don't block writers
- Writers don't block readers
- Faster writes (sequential append)

---

## Architecture Comparison Summary

See **[architecture_comparison.md](./architecture_comparison.md)** for comprehensive analysis covering:

1. **Process Model:** Client-server vs in-process library
2. **Storage Architecture:** Multi-file vs single-file
3. **Concurrency:** MVCC vs file-level locking
4. **Transactions:** Isolation levels and durability
5. **Security:** User auth vs filesystem permissions
6. **Performance:** Benchmarks and trade-offs
7. **Use Cases:** Decision framework for choosing the right database


### Key Takeaways

| Aspect | PostgreSQL | SQLite3 |
|--------|------------|---------|
| **Best For** | Multi-user web services | Embedded/mobile/desktop apps |
| **Architecture** | Client-server (separate daemon) | In-process library |
| **Concurrency** | MVCC (many writers) | File locks (1 writer) |
| **Setup** | Complex (server, config) | Zero (just a file) |
| **Performance** | Better for concurrent writes | Better for single-process reads |
| **Security** | Users, roles, SSL | Filesystem permissions only |
| **Scaling** | Horizontal (replication) | Vertical (single file) |

**The Golden Rule:**
> Use SQLite for **applications**.  
> Use PostgreSQL for **services**.

---

## Building and Running

### Compile C++ Demo

```bash
chmod +x compile.sh
./compile.sh
```

Or manually:
```bash
g++ -std=c++17 -O2 -Wall -Wextra -o sqlite_demo sqlite_demo.cpp -lsqlite3
```

### Run C++ Demo

```bash
./sqlite_demo
```

### Explore PRAGMA Commands

```bash
sqlite3 students.db < pragma_exploration.sql
```

### Test mmap Performance

```bash
sqlite3 students.db < test_mmap.sql
```

### Interactive SQL

```bash
sqlite3 students.db
sqlite> SELECT * FROM students WHERE gpa > 3.5;
sqlite> .quit
```

---

## References

- SQLite Documentation: https://www.sqlite.org/docs.html
- PRAGMA Statements: https://www.sqlite.org/pragma.html
- SQLite File Format: https://www.sqlite.org/fileformat.html
- WAL Mode: https://www.sqlite.org/wal.html
- Memory-Mapped I/O: https://www.sqlite.org/mmap.html
- Lab Session Requirements: `../lab_sessions/lab_2.txt`

---

## Author

**Pulasari Jai** (Roll No: 24BCS10656)  
Advanced Database Management Systems  
Scaler Academy
