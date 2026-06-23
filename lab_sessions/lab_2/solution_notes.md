# Lab 2 — SQLite3 Internals: mmap, Page Size & Library Architecture

## Concept

SQLite is not a database server — it is a C library that your process links against and calls directly. It reads and writes a single `.db` file on disk using OS syscalls. Understanding its storage model requires looking at how it divides that file into fixed-size pages and how it optionally uses `mmap()` to bypass `read()` syscalls entirely.

## Approach

1. Create a SQLite database and populate it with a `students` table.
2. Use `PRAGMA` commands to introspect the internal storage layout — page size, page count, mmap setting, journal mode, cache size.
3. Demonstrate that no separate server process exists (`ps aux | grep sqlite`).
4. Toggle `mmap_size` and understand what changes at the syscall level.
5. Compare the architecture against PostgreSQL.

## Solution

### Files
- `setup.sql` — creates the `students` table and inserts 5 rows
- `pragma_demo.sh` — runs all PRAGMA introspection commands and verifies library vs server behaviour

### PRAGMA results on this machine:

| PRAGMA | Value | What it means |
|---|---|---|
| `page_size` | 4096 | Each page = 4 KB (matches OS page, set at creation) |
| `page_count` | 2 | Page 1 = schema/header, Page 2 = students data |
| File size | 8192 bytes | `4096 × 2` |
| `mmap_size` | 0 | Default: reads use `read()` syscalls |
| `mmap_size=268435456` | 268435456 | 256 MB window: reads become direct memory accesses |
| `journal_mode` | delete | Rollback journal, deleted after each commit |
| `cache_size` | 2000 | ~8 MB page cache in RAM |
| `integrity_check` | ok | All pages valid |

### mmap vs read():
- **mmap off**: kernel copies each page from page cache → userspace buffer on every `read()` call. Two copies of the data exist in memory.
- **mmap on**: the file is mapped directly into the process address space. Reads are memory accesses — no syscall, no extra copy. Faster for large sequential reads.
- On an 8 KB database the difference is invisible (the whole file fits in a single OS fetch).

### SQLite is a library:
```bash
ps aux | grep sqlite   # → nothing. No daemon.
otool -L $(which sqlite3)   # → SQLite is statically embedded on macOS
```
There is no TCP port, no authentication handshake, no IPC. Your code calls `sqlite3_open()` like calling any other function.

## Key Takeaway

SQLite's in-process, single-file design makes it unbeatable for portability — ship the `.db` and it works everywhere. PostgreSQL's client-server MVCC design makes it unbeatable for concurrent multi-user writes. The page size difference (SQLite 4 KB vs PostgreSQL 8 KB) reflects their different I/O targets.
