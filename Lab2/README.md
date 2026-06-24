# Lab 2: SQLite3 Internals + PostgreSQL vs SQLite Design Doc

## Overview

This lab covers two things:
1. SQLite3 internals — mmap, PRAGMA settings, WAL mode, page cache
2. A design comparison of PostgreSQL vs SQLite3 architecture

---

## Files

| File | Purpose |
|------|---------|
| `lab2_comparison.sh`    | PostgreSQL vs SQLite setup, performance, feature comparison |
| `lab2_mmap_internals.sh`| SQLite PRAGMA inspection, mmap enabling, WAL vs DELETE journal |

---

## How to Run

```bash
# comparison script (needs postgresql + sqlite3)
chmod +x lab2_comparison.sh
./lab2_comparison.sh

# mmap + internals script (needs sqlite3, optionally strace)
chmod +x lab2_mmap_internals.sh
./lab2_mmap_internals.sh
```

---

## Part 1: SQLite3 Internals

### PRAGMA Commands

PRAGMAs are SQLite's way of reading and setting internal configuration:

| PRAGMA | What it controls |
|--------|-----------------|
| `page_size` | Size of each I/O unit between SQLite and OS (default 4096 bytes) |
| `page_count` | Total pages in the database file |
| `cache_size` | How many pages SQLite holds in its in-memory page cache |
| `mmap_size` | How many bytes to memory-map (0 = disabled by default) |
| `journal_mode` | Crash recovery strategy (DELETE or WAL) |
| `synchronous` | How aggressively SQLite calls fsync() |
| `encoding` | Text encoding (UTF-8 default) |

### mmap (Memory-Mapped I/O)

By default SQLite uses `read()`/`write()` syscalls:
```
read() → kernel copies file data → user buffer → SQLite
```

With mmap enabled (`PRAGMA mmap_size = 268435456`):
```
mmap() → file mapped into process address space
       → no copy needed
       → OS page cache IS the SQLite buffer
```

mmap is faster for read-heavy workloads because it eliminates the kernel→userspace copy. The tradeoff is that error handling is harder (bus errors instead of return codes).

### WAL vs DELETE Journal Mode

**DELETE (default rollback journal):**
- Before modifying a page, original is written to a `.db-journal` file
- On crash, journal is replayed to restore original state
- Writers block readers

**WAL (Write-Ahead Log):**
- New writes go to a separate `-wal` file
- Readers continue reading from the main `.db` file
- Writer and readers don't block each other
- Better for concurrent access

```bash
PRAGMA journal_mode=WAL;   -- enable WAL
```

---

## Part 2: PostgreSQL vs SQLite3 Design Document

### Architecture

| Aspect | PostgreSQL | SQLite |
|--------|-----------|--------|
| Architecture | Client-server | Serverless, embedded |
| Process model | One process per connection | Single process, single writer |
| Storage | Heap files + separate catalog | Single `.db` file |
| Page size | 8KB default | 4KB default (configurable) |
| Buffer pool | `shared_buffers` (Clock Sweep) | Page cache (LRU) |
| Concurrency | MVCC — multiple readers + writers | WAL — one writer, many readers |
| Crash recovery | WAL (always on) | Rollback journal or WAL |

### How Each Handles a Read Query

**PostgreSQL:**
```
Client → libpq → TCP socket → postmaster → backend process
→ parse → plan → execute
→ check shared_buffers (Clock Sweep buffer pool)
→ if miss: read from heap file on disk → load into shared_buffers
→ return rows
```

**SQLite:**
```
Application → SQLite library (in-process, no network)
→ parse → plan → execute
→ check page cache
→ if miss: read() or mmap() from .db file
→ return rows
```

### How Each Handles a Write

**PostgreSQL:**
```
BEGIN → write to WAL (pg_wal/) first
     → modify page in shared_buffers
     → COMMIT → WAL record flushed to disk
     → dirty pages written to heap file later (background writer)
```

**SQLite (WAL mode):**
```
BEGIN → write new page version to -wal file
     → COMMIT → wal record fsynced
     → checkpoint: eventually merge -wal back into .db
```

### When to Use Which

| Scenario | Use |
|----------|-----|
| Mobile / embedded app | SQLite |
| Local prototype / scripts | SQLite |
| Production web backend | PostgreSQL |
| Multi-user concurrent writes | PostgreSQL |
| Need JSON, arrays, custom types | PostgreSQL |
| Zero-config single file storage | SQLite |
| Read-heavy analytics on small data | SQLite + mmap |
| Replication, logical decoding | PostgreSQL |

### Key Design Difference

SQLite's entire philosophy is **zero-config simplicity** — one file, no server, no setup. It trades write concurrency for portability.

PostgreSQL's philosophy is **correctness and power at scale** — full ACID with MVCC, rich type system, extensions, replication. It requires more setup but handles anything you throw at it.

Neither is better — they solve different problems.

---

## Author
Submitted as part of Lab 2 – Advanced DBMS Lab  
Date: May 2026