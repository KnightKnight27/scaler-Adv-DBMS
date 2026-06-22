# Lab Session 2 — Solution: SQLite3 Internals (mmap, Page Size, PRAGMA & Library Architecture)

This directory contains my completed solution to **`lab_sessions/lab_2.txt`**,
implemented in **C++ against `libsqlite3`** (matching Lab 1's C++ approach and the
lab's own `sqlite3.h` code snippet).

The lab asks me to inspect SQLite's storage internals through `PRAGMA` commands,
prove that SQLite is an **in-process library** (not a server), and connect those
findings to System Design Assignment 1 (PostgreSQL vs SQLite).

Everything below is reproduced from **real runs on this machine**
(Ubuntu 24.04, SQLite **3.45.1**, `libsqlite3-dev` installed), against the
repository's own [`students.db`](students.db).

> **Tooling note.** The standalone `sqlite3` CLI was not installed, but the
> development library **`libsqlite3-dev` is** (`/usr/include/sqlite3.h` +
> `/usr/lib/x86_64-linux-gnu/libsqlite3.so`). Since the lab's Part 3 is
> explicitly about calling SQLite **from C++** (`sqlite3_open`, `sqlite3_exec`,
> `sqlite3_close` — "all in-process function calls"), I did the whole lab as C++
> programs linked with `-lsqlite3`. This is both closer to the lab's intent and
> a cleaner demonstration than the CLI would have been.

## Files in this directory

| File | Purpose |
|------|---------|
| `sqlite_internals.cpp` | Part 2 + Part 3 — PRAGMA introspection and in-process proof |
| `query.cpp` | Minimal driver for the mmap-vs-read `strace` experiment (arg = `mmap_size`) |
| `hold.cpp` | Holds the DB open so the fd can be inspected via `/proc` |
| `students.db` | The SQLite database under test (4 pages, 16 KiB) |
| `strace_nommap.txt` | Captured trace of `./query 0` (I/O via `pread64`) |
| `strace_mmap.txt` | Captured trace of `./query 268435456` (I/O via `mmap`) |
| `.gitignore` | Excludes the compiled binaries |

```bash
# Build everything:
g++ -O2 -o sqlite_internals sqlite_internals.cpp -lsqlite3
g++ -O2 -o query           query.cpp            -lsqlite3
g++ -O2 -o hold            hold.cpp             -lsqlite3
```

---

## Part 1 — Installation & Verification

```bash
# The lab's intended path:
sudo apt install sqlite3 libsqlite3-dev

# What this machine has: libsqlite3-dev (header + linkable .so), verified by
# compiling and calling sqlite3_libversion() from C++:
$ ./sqlite_internals students.db | head -1
sqlite3 library version : 3.45.1
```

The engine version is **3.45.1** — exactly the release the lab references.

---

## Part 2 — Storage Internals via PRAGMA

[`sqlite_internals.cpp`](sqlite_internals.cpp) opens `students.db` with
`sqlite3_open()` and runs each PRAGMA via `sqlite3_prepare_v2`/`sqlite3_step`.

**Real captured output:**

```
=== Part 2: storage internals via PRAGMA (students.db) ===
PRAGMA page_size        -> 4096
PRAGMA page_count       -> 4
PRAGMA freelist_count   -> 0
PRAGMA mmap_size        -> 0
PRAGMA journal_mode     -> delete
PRAGMA cache_size       -> -2000
PRAGMA encoding         -> UTF-8
PRAGMA schema_version   -> 1
PRAGMA integrity_check  -> ok
page_size * page_count  -> 16384      ✅ equals the on-disk file size exactly
```

Interpretation of each value:

- **`page_size = 4096`** — the whole DB is one file split into fixed 4 KiB pages
  (matches the OS page size). Set at creation; immutable without `VACUUM INTO`.
- **`page_count = 4`** ⇒ **4 × 4096 = 16384 bytes**, *exactly* the file size.
  This is the foundational SQLite invariant: **the file is nothing but an array
  of equal-size pages.**
- **`freelist_count = 0`** — no pages freed; all 4 are live.
- **`mmap_size = 0`** — memory-mapped I/O is **off by default** (key for the
  experiment below).
- **`journal_mode = delete`** — the classic rollback-journal mode (a
  `students.db-journal` appears during a write, deleted on commit), *not* WAL.
  This matches the file-header byte decoded below.
- **`cache_size = -2000`** — a *negative* value means **2000 KiB** (≈2 MB) of page
  cache; a positive value would mean a count of pages. A commonly misread PRAGMA.
- **`integrity_check = ok`** — every page validated.

### Cross-checking the PRAGMAs against the raw file header (depth)

SQLite's first 100 bytes are a documented header. Dumping them directly:

```bash
$ od -A d -t x1 students.db | head -2
0000000 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00   # "SQLite format 3\0"
0000016 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 04
```

| Offset | Bytes | Field | Decodes to | Matches PRAGMA? |
|--------|-------|-------|-----------|-----------------|
| 0–15 | `53 51 4c ... 33 00` | Magic string | `"SQLite format 3\0"` | ✅ it's a SQLite DB |
| 16–17 | `10 00` | Page size | `0x1000 = 4096` | ✅ `page_size` |
| 18 | `01` | Write version | `1 = rollback journal` | ✅ `journal_mode=delete` |
| 19 | `01` | Read version | `1 = legacy` | ✅ |
| 24–27 | `00 00 00 02` | File change counter | `2` | (bumped per write txn) |
| 28–31 | `00 00 00 04` | **Size in pages** | `4` | ✅ `page_count` |

All three independent views — `PRAGMA`, the raw `od` header dump, and the bytes
that `strace` shows being `pread`'d (next section) — agree perfectly.

### The schema being stored

```sql
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name  VARCHAR(100) NOT NULL,
    age        INT,
    email      VARCHAR(255) UNIQUE,
    course     VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);   -- 2 rows; + auto-indexes for PRIMARY KEY and UNIQUE(email)
```

> Side observation: `SERIAL` is not a real SQLite type. SQLite's dynamic typing
> accepts the declaration but does not enforce it, so `student_id` is not
> auto-incremented the way it would be in PostgreSQL — a reminder that SQLite
> treats declared types as *affinity hints*, not constraints.

### The mmap experiment — *the* core of Part 2

Goal: prove that `mmap_size > 0` changes SQLite's I/O from `read`-style syscalls
into a single `mmap`, after which page access is plain memory access.

I traced [`query.cpp`](query.cpp) (runs `SELECT count(*) FROM students` with the
page cache disabled, so I/O is forced) twice. Because a native C++ binary doesn't
drag in an interpreter's startup, the traces are tiny (47 lines) and the DB file
descriptor is cleanly **fd 3** throughout:

**(A) `./query 0` — `mmap_size = 0` (default):**
```
openat(AT_FDCWD, ".../students.db", O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC) = 3
pread64(3, "SQLite format 3\0\20\0\1\1\0@  \0\0\0\2\0\0\0\4"..., 100,  0)    = 100   # header
pread64(3, "SQLite format 3..."..., 4096, 0)                                = 4096  # page 1
pread64(3, "\0\0\0\2\0\0\0\4...", 16, 24)                                   = 16    # change counter
pread64(3, "SQLite format 3..."..., 4096, 0)                                = 4096
pread64(3, "\n\0\0\0\2\17\367..."..., 4096, 8192)                           = 4096  # the DATA page (offset 8192)
```
→ **5 `pread64` calls** on the DB; **0 `mmap` of the DB file.**

**(B) `./query 268435456` — `mmap_size = 256 MB`:**
```
openat(AT_FDCWD, ".../students.db", ...) = 3
pread64(3, "SQLite format 3..."..., 100,  0)   = 100    # still must read header first
pread64(3, "SQLite format 3..."..., 4096, 0)   = 4096
pread64(3, "...", 16, 24)                       = 16
pread64(3, "SQLite format 3..."..., 4096, 0)   = 4096
mmap(NULL, 16384, PROT_READ, MAP_SHARED, 3, 0) = 0x...   # <-- maps the ENTIRE file
```
→ **4 `pread64` + 1 `mmap`.** The data-page read at offset 8192 present in (A) is
**gone** — once the file is mapped, reading that page is a CPU load from the
mapping, invisible to `strace`.

Two precise observations that make this convincing:

1. **The `mmap` length is `16384` = `page_size × page_count`** — SQLite mapped
   the *whole database file* (it maps `min(mmap_size, file_size)`), `MAP_SHARED`
   so the mapping and the OS page cache share the same physical pages (zero-copy).
2. **SQLite still `pread`s the 100-byte header even in mmap mode**, because it
   must learn the page size *before* it can set up the mapping — a chicken-and-egg
   detail visible only at the syscall level.

```bash
# Reproduce:
strace -e trace=openat,pread64,read,mmap ./query 0          2> strace_nommap.txt
strace -e trace=openat,pread64,read,mmap ./query 268435456  2> strace_mmap.txt
```

---

## Part 3 — SQLite3 is a Library, Not a Process

The architecturally decisive difference from PostgreSQL — verified three ways
with the C++ binaries.

**1. There is no SQLite server/daemon.** Nothing like a `postgres` process
exists; the only thing that ever touches the DB is my own program. (A
`ps aux | grep sqlite` shows no daemon — any match is just the grep/command text
itself, never a resident server.)

**2. The engine is linked *into* the application as a shared library:**
```bash
$ ldd ./sqlite_internals | grep sqlite
libsqlite3.so.0 => /lib/x86_64-linux-gnu/libsqlite3.so.0 (0x0000...)
```
My C++ binary dynamically links `libsqlite3.so.0` — exactly the
`ldd $(which sqlite3)` relationship the lab describes, here for my own program.
The entire database engine ships as that `.so` and runs in my address space.

**3. The DB engine executes inside my own process.** `sqlite_internals` prints
its PID and does all the work itself:
```
application PID : 107914   (the DB engine runs INSIDE this process)
...
closed — no IPC, no daemon involved; PID 107914 did everything itself.
```
And while [`hold.cpp`](hold.cpp) keeps a connection open, the database is just an
ordinary file descriptor owned by **my** process (same kind of `/proc` check as
Lab 1):
```bash
$ ./hold &              # prints: holding students.db open, PID 108232
$ ls -l /proc/108232/fd | grep students.db
lrwx------ ... 3 -> /home/ratnesh/.../Lab2-Assignment/students.db
```

```
  My C++ process (e.g. PID 108232)
  ├── libsqlite3.so.0           ← the entire DB engine, in-process
  │     └── fd 3 → students.db  ← reads/writes the file directly via pread64/mmap
  └── (no socket, no daemon, no port 5432)
```

Contrast with PostgreSQL, where a client `connect()`s a TCP/Unix socket to a
separate long-lived `postgres` server that owns the data files — a context
switch and IPC round-trip per interaction.

---

## System Design Assignment 1 link — PostgreSQL vs SQLite3

| Dimension | SQLite3 (verified above) | PostgreSQL |
|-----------|--------------------------|------------|
| Process model | **Library in your process** (`libsqlite3.so` via `ldd`, fd 3 → file, PID confirmed) | Client-server: separate `postgres` daemon |
| Communication | Direct C function calls + `pread64`/`mmap` on the file | TCP (5432) or Unix socket |
| Concurrency | File locks; one writer at a time (`journal_mode=delete` here; WAL improves it) | MVCC: many concurrent readers **and** writers |
| Authentication | None — filesystem permissions only | Roles/passwords/SSL |
| Storage | Single `.db` file = array of `page_size` pages (4 KiB × 4 here) | Data directory of many files + WAL |
| I/O path | OS page cache, optionally `mmap` (shown) | Own `shared_buffers` pool, managed by the server |

**How `mmap` fits in:** SQLite can map the `.db` directly into the process
(`MAP_SHARED`, shown above) so the OS page cache and the process share physical
pages — fast, zero-copy reads. PostgreSQL instead manages its own
`shared_buffers` inside the server and does not use `mmap` for its primary path.

**Why SQLite suits mobile/embedded:** zero infrastructure — no daemon, no port,
no auth handshake; the whole DB is one portable file and the engine is a small
`.so` linked into the app. A query is a function call, not a network hop.

**Why PostgreSQL suits large multi-user systems:** a central server with MVCC
lets many clients read and write concurrently with real isolation levels, roles,
and auth — which a single-file, file-locked, single-writer library cannot offer.

---

## Key Learnings

- **A SQLite database file is literally an array of fixed-size pages**:
  `page_size × page_count = 16384 = file size`, confirmed three independent ways
  (`PRAGMA`, the raw `od` header dump, and the bytes `strace` shows being read).
- **`mmap_size` materially changes the syscall pattern**: off → page reads are
  `pread64`; on → SQLite issues one `MAP_SHARED` `mmap` of the whole file and
  subsequent page access is invisible memory access — yet it *still* `pread`s the
  header first to discover the page size.
- **SQLite is unambiguously a library, not a server**: no daemon, the engine is
  `libsqlite3.so` linked into my C++ binary (`ldd`), and the DB is just an fd
  owned by my own PID. This single fact drives nearly every trade-off vs
  PostgreSQL (concurrency, auth, deployment, latency).
- **SQLite's dynamic typing is real**: a `SERIAL PRIMARY KEY` column did not
  behave like PostgreSQL's — declared types are affinity hints, not enforced.

---

### Reference

- Solution to `lab_sessions/lab_2.txt` (Advanced DBMS lab series).
- SQLite C/C++ interface (`sqlite3_open`, `sqlite3_prepare_v2`, `sqlite3_exec`) and the "Database File Format" header layout: official SQLite documentation.
- Tools used: `g++` 13.3.0 + `libsqlite3` **3.45.1**, `strace`, `ldd`, `od`, `/proc` — Ubuntu 24.04, x86-64.
