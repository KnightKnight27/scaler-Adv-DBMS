# Lab 2: SQLite3 Internals — mmap, Page Size, PRAGMA & Library Architecture

## Observations

### Page Size
```sql
sqlite3 students.db
PRAGMA page_size;
-- Result: 4096
```
SQLite stores the entire database as a single file divided into 4096-byte pages. This matches the OS page size so each SQLite page maps to exactly one OS page — no partial reads.

### Page Count
```sql
PRAGMA page_count;
-- Result: 5 (varies with data)
```
Total file size = 4096 * 5 = 20480 bytes = 20 KB.

### mmap Size
```sql
PRAGMA mmap_size;
-- Default: 0 (disabled)

PRAGMA mmap_size = 268435456;  -- enable 256 MB mmap
PRAGMA mmap_size;
-- Result: 268435456
```

With mmap enabled, SQLite calls `mmap()` on the .db file. The OS maps file pages directly into process address space — reads become memory accesses instead of `read()` syscalls.

### strace comparison

Without mmap:
```
read(3, "\x53\x51\x4c\x69...", 4096) = 4096   # SQLite header page
read(3, "...", 4096)                  = 4096   # each page = one read()
```

With mmap:
```
mmap(NULL, 268435456, PROT_READ, MAP_SHARED, 3, 0) = 0x7f...  # one call
# Subsequent accesses are page faults, not read() syscalls
```

### SQLite is a Library, Not a Process

```bash
ps aux | grep sqlite
# Nothing — only your own process appears

ldd $(which sqlite3)
# libsqlite3.so.0 => /lib/x86_64-linux-gnu/libsqlite3.so.0
```

SQLite runs **inside** your application process. No TCP socket, no server daemon, no authentication handshake.

## Architecture Comparison

| Dimension     | SQLite3                              | PostgreSQL                          |
|---------------|--------------------------------------|-------------------------------------|
| Process model | In-process library                   | Separate server daemon              |
| Communication | Direct function calls                | TCP/Unix socket (port 5432)         |
| Concurrency   | File locks (WAL improves this)       | MVCC — many readers + writers       |
| Storage       | Single `.db` file                    | Data directory + WAL files          |
| Transactions  | ACID (serialized writes)             | Full ACID with MVCC isolation       |

## Key Insight

SQLite's mmap optimization bypasses `read()` syscall overhead — the OS page cache and the process share the same physical memory pages directly. PostgreSQL manages its own `shared_buffers` pool and does not rely on mmap for its primary I/O path, giving it more control over eviction policy (clock-sweep) but adding complexity.
