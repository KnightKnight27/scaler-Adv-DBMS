# PostgreSQL vs SQLite — Architecture Comparison

**Author:** Ankur Kalita
**Roll Number:** 23BCS10185
**Course:** Advanced DBMS — System Design Discussion

> This document compares PostgreSQL and SQLite as two deliberately *opposite*
> points in the design space of relational databases. PostgreSQL is a
> client-server system built for many concurrent users; SQLite is an embedded
> library that lives inside a single process. Almost every difference between
> them — file layout, concurrency, durability, performance — falls out of that
> one architectural choice. The benchmark numbers and the on-disk hex walks
> below come from my own lab work (Lab 1, Lab 2, Lab 4) on this dataset.

---

## 1. Problem Background

### Why these two systems exist

Relational databases all answer the same question — *"how do I store rows and
query them safely?"* — but they answer it for very different deployment shapes.

**SQLite (2000, D. Richard Hipp).** SQLite was created to be a database with
**no server**. It began as a library to run inside an application where you
could not assume a database administrator, a daemon, or even a network. The
entire database is a **single file** that the host program opens with ordinary
`open()`/`read()`/`write()` system calls. The design goal was *zero
configuration, zero operations, local-first storage*. It is today the most
widely deployed database in the world precisely because it ships *inside* other
software — browsers, phones, embedded devices — rather than running as its own
service.

**PostgreSQL (1986 → POSTGRES at Berkeley, SQL support 1996).** PostgreSQL was
built as a **multi-user server**. It assumes many clients connect concurrently
over a socket, that writers and readers run at the same time, and that the
database must survive crashes without losing committed data. Its design goal
was *correctness and concurrency at scale* — a full RDBMS with a cost-based
planner, MVCC, and write-ahead logging.

### The core problem each one optimizes

| | SQLite | PostgreSQL |
|---|---|---|
| Primary constraint | "There is no server and no DBA." | "Many users hit this at once and must not corrupt each other." |
| Optimizes for | Simplicity, zero-ops, single-file portability | Concurrency, durability, query sophistication |
| The cost it accepts | Weak multi-writer concurrency | Operational complexity (a running server, tuning, vacuum) |

Everything that follows is downstream of this single split: **embedded library
vs. client-server process model.**

---

## 2. Architecture Overview

### 2.1 SQLite — the embedded model

There is no separate process. The application links the SQLite library and
calls into it as function calls. SQLite then talks to **one file on disk**.

```
        ┌──────────────────────────────────────────┐
        │            Application process            │
        │                                            │
        │   your code ── func call ──► SQLite lib   │
        │                                │           │
        │                       ┌────────▼────────┐  │
        │                       │  SQL compiler   │  │
        │                       │  VDBE (bytecode)│  │
        │                       │  B-tree layer   │  │
        │                       │  Pager + cache  │  │
        │                       └────────┬────────┘  │
        └────────────────────────────────┼──────────┘
                                          │ read()/write()/fsync() syscalls
                                          ▼
                              ┌───────────────────────┐
                              │   single .db file     │
                              │  + -wal / -journal    │
                              └───────────────────────┘
```

**Data flow for a query:** SQL text → compiled to VDBE bytecode → the bytecode
drives the B-tree layer → the pager fetches 4 KB pages (from its private cache
or via a `read()` syscall) → rows decoded and returned — all *in-process*, no
network hop, no second process.

### 2.2 PostgreSQL — the client-server model

A client connects over a socket. A **postmaster** daemon forks a dedicated
**backend process** per connection. All backends share memory (the
`shared_buffers` pool) and a set of background workers (WAL writer,
checkpointer, autovacuum, background writer).

```
   client ──TCP/unix socket──►  postmaster (daemon)
                                    │ forks one backend per connection
        ┌───────────────────────────┼───────────────────────────┐
        ▼                           ▼                            ▼
   backend #1                  backend #2                   backend #N
   parse→plan→execute          parse→plan→execute           ...
        │                           │                            │
        └──────────┬────────────────┴───────────┬───────────────┘
                   ▼                             ▼
        ┌───────────────────────┐   ┌────────────────────────────┐
        │   shared_buffers      │   │  background processes:      │
        │  (shared page cache)  │   │  WAL writer, checkpointer,  │
        └──────────┬────────────┘   │  autovacuum, bgwriter       │
                   │                 └────────────────────────────┘
                   ▼
   data directory:  base/<oid> heap+index files  +  pg_wal/ (WAL segments)
```

**Data flow for a query:** SQL arrives on a socket → backend **parses** → the
**cost-based planner** picks a plan using `pg_statistic` → the **executor**
pulls pages through `shared_buffers` → changes are first written to **WAL**
(durability), then to heap pages → result streamed back over the socket.

### 2.3 The same thing, side by side

| Component | SQLite | PostgreSQL |
|---|---|---|
| Process model | In-process library | Daemon + one backend process per connection |
| Where the data lives | One `.db` file | A *data directory* of many files |
| Shared memory | None (per-connection cache) | `shared_buffers` shared across backends |
| Background workers | None | WAL writer, checkpointer, autovacuum, bgwriter |
| Client transport | Function call | TCP / Unix socket |

---

## 3. Internal Design

### 3.1 On-disk layout

**SQLite = one file = N fixed-size pages laid end to end.** From my Lab 4 hex
walk of a real SQLite DB (`pokedex.db`, page size 4096 B, 6 pages):

```
offset  0          4096        8192       12288      16384      20480      24576
        ├──Page 1───┼──Page 2───┼──Page 3──┼──Page 4──┼──Page 5──┼──Page 6──┤
        │ file hdr  │ pokemon   │  leaf    │  leaf    │  leaf    │  leaf    │
        │ + schema  │ INTERIOR  │ rowids   │ rowids   │ rowids   │ rowids   │
        │ b-tree    │ (root)    │ 1..32    │ 33..64   │ 65..97   │ 98..100  │
        └───────────┴───────────┴──────────┴──────────┴──────────┴──────────┘
```

The first 100 bytes of page 1 are the file header, beginning with the literal
magic string `"SQLite format 3\0"`, with the page size stored at offset 16
(`0x1000 = 4096`). Every table is a **B-tree of pages**; the schema itself
(`sqlite_schema`) is just another B-tree on page 1 that maps a table name to
its root page.

**PostgreSQL = a directory of files.** Each table (and each index) is one or
more heap files under `base/<db-oid>/<relation-oid>`, split into 1 GB segments,
each made of 8 KB pages. Alongside them lives `pg_wal/` (the write-ahead log)
and catalog tables. Multiple files per table is what lets PostgreSQL grow
tables, indexes, and the WAL independently.

| | SQLite | PostgreSQL |
|---|---|---|
| Page size (observed in labs) | **4 KB** | **8 KB** (16 KB for MySQL/InnoDB) |
| Files per table | 0 (shared single file) | ≥ 1 heap file + index files + WAL |
| Schema storage | `sqlite_schema` B-tree on page 1 | system catalogs (`pg_class`, …) |

### 3.2 Page internals — the slotted page

Both engines use the classic **slotted page**: a header grows down from the
top, a *cell-pointer array* grows down behind it, and the actual row/cell
content grows *up* from the bottom. The gap in the middle is free space; when
it hits zero, the page **splits**. From my Lab 4 walk of an SQLite page:

```
offset 0                                                            offset 4095
├─────────┬───────────────┬──────────────────────────────┬───────────────┤
│ Page    │ Cell pointer  │                              │  Cell content │
│ header  │ array         │   ← unallocated / free →     │  (grows back  │
│ 8 or 12 │ N × 2 bytes   │                              │   towards     │
│ bytes   │               │                              │   middle)     │
└─────────┴───────────────┴──────────────────────────────┴───────────────┘
```

The principle is identical in PostgreSQL (its page = header + line-pointer
array + tuples growing up from the end). This is a genuinely shared idea across
real databases, which is why studying SQLite's bytes teaches you PostgreSQL's
layout too.

### 3.3 Index organization

Both default to **B-trees**, but the relationship between the index and the row
differs:

- **SQLite** tables are stored *as* a B-tree keyed on `rowid` (an implicit
  integer key) — the table **is** the index. `INTEGER PRIMARY KEY` becomes an
  alias for the rowid, so the primary key lookup is the table walk itself.
  Interior cells are tiny (in my Lab 4 DB, 5 bytes each: a 4-byte child page
  pointer + a 1-byte rowid varint), so a single interior node fans out to
  hundreds of children and the tree stays shallow — a 100-row table needs only
  3 page reads to find any row.
- **PostgreSQL** stores the table as an unordered **heap** and indexes are
  *separate* B-tree files (the `nbtree` access method) that point at heap
  tuples via a `(page, offset)` tuple id (TID). A primary key is a unique
  B-tree index sitting beside the heap, not the heap itself.

```
SQLite:  PRIMARY KEY  ==  the table B-tree           (index-organized)
Postgres: heap (unordered)  +  separate nbtree index ──TID──► heap tuple
```

This is why a clustered/rowid lookup in SQLite is one tree; in PostgreSQL it is
"search the index tree, then jump to the heap."

### 3.4 Lookup walkthrough (from Lab 4)

`SELECT * FROM pokemon WHERE id = 42` on the SQLite DB:

1. Read **page 1**, scan `sqlite_schema` → pokemon's `rootpage = 2`.
2. Read **page 2** (type byte `0x05` = interior). Keys are 32, 64, 97. `42 > 32`
   and `42 ≤ 64` → descend to child **page 4**.
3. Read **page 4** (type byte `0x0D` = leaf). Binary-search its cell-pointer
   array for rowid 42, decode the record, return columns.

**Three page reads** for the whole lookup. PostgreSQL does the morally
equivalent walk on its `nbtree` index, then one extra fetch into the heap.

### 3.5 Transaction management & concurrency control

This is where the two architectures diverge the most.

**SQLite** historically used a **rollback journal**: before modifying a page it
copies the original page into a side journal, so a crash can roll back. Modern
SQLite usually runs in **WAL mode** instead (a `-wal` file), which lets readers
keep reading the last committed snapshot while a single writer appends. But the
hard limit remains: **one writer at a time for the whole database.** Writes take
a database-level lock. There is no per-row locking and no parallel execution.

**PostgreSQL** uses **MVCC (Multi-Version Concurrency Control)**. Every row
version (tuple) carries hidden system columns **`xmin`** (the transaction that
created it) and **`xmax`** (the transaction that deleted/superseded it). An
`UPDATE` does **not** overwrite — it writes a *new* tuple version and marks the
old one's `xmax`. Each transaction sees a **snapshot**: a tuple is visible if
its `xmin` committed before the snapshot and its `xmax` has not. The payoff is
the defining MVCC property: **readers never block writers and writers never
block readers.** (This is exactly the model I implemented by hand in Lab 8 —
visibility by `xmin`/`xmax` plus two-phase locking for write conflicts.)

| | SQLite | PostgreSQL |
|---|---|---|
| Concurrency model | DB-level lock, single writer | MVCC, many concurrent readers + writers |
| Update strategy | In-place (with journal/WAL for rollback) | Append new tuple version, mark old `xmax` |
| Readers vs writers | WAL mode: readers don't block the writer | Readers and writers never block each other |
| Cleanup of old versions | n/a (in-place) | **VACUUM** reclaims dead tuples |

The cost of PostgreSQL's MVCC is **dead tuples**: old versions pile up and must
be reclaimed by `VACUUM`. That overhead is visible in the file sizes in §5
(PostgreSQL's DB is larger partly because it carries version/visibility info).

### 3.6 Durability & recovery

**SQLite** gets durability from its journal/WAL plus an `fsync()` at commit. In
classic journal mode, the journal is synced before the change is applied; in WAL
mode the WAL is synced and a *checkpoint* later folds it back into the main
file. Crash recovery replays/rolls back from whichever file survived.

**PostgreSQL** uses **Write-Ahead Logging (WAL)** as a first-class subsystem.
The rule: **the WAL record describing a change is flushed to disk *before* the
data page is.** On `COMMIT`, the WAL up to that point is `fsync`-ed — that flush
*is* the durability guarantee, even though the heap page may still be dirty in
`shared_buffers`. A **checkpoint** periodically flushes dirty pages and lets old
WAL be recycled. After a crash, PostgreSQL **replays** WAL from the last
checkpoint to restore every committed change. WAL also underpins replication and
point-in-time recovery.

```
        change made → WAL record written → WAL fsync at COMMIT  ← durability point
                                              │
              (later) checkpoint flushes dirty heap pages, WAL recycled
              (after crash) replay WAL from last checkpoint forward
```

### 3.7 Memory management

- **SQLite:** a per-connection page cache, plus optional **`mmap_size`** to map
  the file directly into the process address space and skip the `read()` copy.
  No shared memory between processes.
- **PostgreSQL:** a single shared **`shared_buffers`** pool used by every
  backend, with clock-sweep buffer replacement and a background writer that
  trickles dirty pages out so checkpoints aren't a stall.

This connects to Lab 1: a `read()` is a syscall into the kernel, which serves
the 4 KB block from the **OS page cache** (a hit) or fetches it from disk (a
miss). SQLite's `mmap_size` aims to skip the extra kernel→userspace copy;
PostgreSQL instead keeps its own large user-space cache so it controls eviction.

---

## 4. Design Trade-Offs

### SQLite

**Advantages**
- Zero configuration, zero operations — no server to run, secure, or tune.
- The whole database is one portable file you can copy, email, or embed.
- No network/IPC overhead; for simple queries it is extremely fast (see Q1 in §5).
- Tiny footprint — ideal for phones, browsers, edge/embedded devices.

**Limitations**
- **One writer at a time** for the entire database — fine for one app, fatal for
  a busy multi-user service.
- No cost-based parallel planner: complex joins fall back to nested loops with
  no hash-join/parallelism, so they can blow up (see Q3 in §5 — it never
  finished).
- No built-in network access, users, or roles.

### PostgreSQL

**Advantages**
- Genuine concurrency via MVCC — readers and writers don't block each other.
- A sophisticated cost-based, parallel-capable planner that wins decisively on
  large joins (§5).
- Strong durability and recovery via WAL; replication and PITR build on it.
- Rich feature set: roles, extensions, types, partitioning.

**Limitations**
- Operationally heavy — a server to run, connections to manage, parameters to
  tune.
- MVCC produces **dead tuples** that `VACUUM` must reclaim; neglecting it causes
  bloat and transaction-ID wraparound risk.
- Per-connection backend processes are relatively heavy (hence connection
  poolers like PgBouncer in production).

### The engineering decision in one line

> SQLite trades concurrency and query sophistication for *radical simplicity*.
> PostgreSQL trades simplicity for *concurrency, correctness, and scale*. There
> is no universally "better" one — only the right tool for the deployment shape.

---

## 5. Experiments / Observations

All numbers below are from **my own Lab 2 benchmark** running the *same* schema,
data, and queries against PostgreSQL, MySQL/InnoDB, and SQLite. Times are
warm-cache wall-clock averages.

**Dataset:** `customers` = 100,000 rows · `orders` = 500,000 rows ·
`order_items` = 1,500,873 rows, with indexes on the join/filter columns.

### Query timings

| Query | PostgreSQL | SQLite | MySQL (for context) |
|---|---|---|---|
| **Q1** — filter + aggregate on `orders` | **31.6 ms** | 111 ms | 733 ms |
| **Q2** — join customers × orders, group by city | **71.9 ms** | 734 ms | 1,047 ms |
| **Q3** — join orders × order_items (500k × 1.5M), top 10 | **549 ms** | **> 20 min (timed out, killed)** | 3,090 ms |

### Storage observed

| | PostgreSQL | SQLite |
|---|---|---|
| DB size | 260 MB | **197 MB** |
| Page size | 8 KB | 4 KB |
| Memory knob | `shared_buffers` (dedicated pool) | `mmap_size` / OS page cache |

### The `mmap_size` experiment (SQLite)

I compared `PRAGMA mmap_size = 0` (disabled) vs `268435456` (256 MB):

| Query | mmap=0 | mmap=256MB | Change |
|---|---|---|---|
| Q1 | 111 ms | 113 ms | ≈ none |
| Q2 | 734 ms | 744 ms | ≈ none |

### What the numbers actually tell us (architecture, not trivia)

- **SQLite wins the *simple* query (Q1: 111 ms vs MySQL's 733 ms).** With no
  server and no network, a single-table aggregate runs entirely in-process —
  the embedded model's best case.
- **SQLite *cannot* do the big join (Q3 timed out after 20+ minutes).** With no
  cost-based optimizer, no hash join, and no parallelism, a 500k × 1.5M join
  degenerates into a nested-loop scan. This is the embedded model's worst case,
  and it is purely architectural — not a bug.
- **PostgreSQL is fastest on every query**, and its lead *grows* with
  complexity (≈3.5× SQLite on Q1, ≈10× on Q2, and SQLite simply can't finish
  Q3). The cost-based parallel planner and shared buffer pool are doing exactly
  what the client-server architecture was built for.
- **SQLite's file is smaller (197 MB vs 260 MB)** even on identical data. A big
  reason: PostgreSQL stores **MVCC visibility info** (`xmin`/`xmax`, dead tuple
  versions) alongside live rows — the price of concurrency. SQLite, with one
  writer, carries none of that overhead.
- **`mmap_size` made no measurable difference on macOS.** macOS already keeps
  file pages warm in its unified buffer cache (the OS page cache from Lab 1), so
  skipping the `read()` copy saves almost nothing when the data is already
  cached. On Linux with a *cold* cache or I/O-bound workloads, `mmap` can
  measurably help — the result is platform- and workload-dependent, which is
  itself the lesson.

### On-disk verification (Lab 4)

Beyond timings, I verified SQLite's structure by hex-dumping a real DB: the
`"SQLite format 3"` magic header, the 4096-byte page size at offset 16, the
interior page type byte `0x05` with split keys 32/64/97 fanning out to four leaf
pages (`0x0D`), and `32 + 32 + 33 + 3 = 100` rows accounted for — confirming the
single-file, page-structured, B-tree-of-pages design described in §3.

---

## 6. Key Learnings

1. **One architectural choice explains almost everything.** Embedded-library vs.
   client-server is the root cause of nearly every difference — file layout,
   concurrency, durability, and the benchmark gaps. If you remember one thing,
   remember that.

2. **Concurrency is the real dividing line.** SQLite's single-writer lock vs.
   PostgreSQL's MVCC (`xmin`/`xmax` + snapshots) is *the* difference that decides
   whether a database can serve many users. Everything else is secondary.

3. **MVCC isn't free — you pay in space and VACUUM.** PostgreSQL's larger file
   (260 MB vs 197 MB on identical data) is the visible cost of keeping multiple
   tuple versions for concurrency. Concurrency is bought with storage overhead
   and a background cleanup process.

4. **The right database depends on the workload, and the benchmark proves it.**
   SQLite *beat* MySQL on the simple Q1 but *could not finish* the big join Q3
   that PostgreSQL did in 549 ms. "Fastest" is meaningless without naming the
   workload. SQLite for local/embedded/zero-ops; PostgreSQL for multi-user,
   join-heavy, durable workloads.

5. **A surprising result: `mmap_size` did nothing on macOS.** I expected
   memory-mapped I/O to help; it didn't, because the OS page cache had already
   warmed the data. Optimizations only pay off when they remove an *actual*
   bottleneck — the kernel was already doing the caching SQLite hoped to
   shortcut.

6. **Real databases share more internals than their marketing suggests.**
   Slotted pages, B-trees, and write-ahead logging show up in *both* engines.
   Reading SQLite's raw bytes (Lab 4) taught me PostgreSQL's page model too —
   the concepts transfer.

---

## References

- D.R. Hipp et al., *SQLite Database File Format* — https://www.sqlite.org/fileformat.html
- *SQLite — Write-Ahead Logging* — https://www.sqlite.org/wal.html
- PostgreSQL Documentation — *Internals: Database Page Layout, MVCC, WAL* —
  https://www.postgresql.org/docs/current/
- Alex Petrov, *Database Internals* (O'Reilly, 2019) — slotted pages, B-trees, WAL, MVCC.
- My own lab work: Lab 1 (file I/O & the `read()`/page-cache path), Lab 2 (the
  PostgreSQL vs SQLite vs MySQL benchmark), Lab 4 (hex-walking SQLite pages and
  B-tree nodes), and Lab 8 (a hand-built MVCC + two-phase-locking transaction
  manager).
