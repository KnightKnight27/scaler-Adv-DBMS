# PostgreSQL vs SQLite — Architecture Comparison

**Author:** Manjari Rathore  
**Roll Number:** 23BCS10192  
**Course:** Advanced DBMS — System Design Discussion

> PostgreSQL and SQLite are at opposite ends of how a relational database can be built. PostgreSQL is a server that many users connect to at the same time. SQLite is a library that lives inside your app and reads a single file. Almost every difference between them comes from that one choice. The numbers and hex-walk observations below are from my own lab work (Lab 1, Lab 2, Lab 4).

---

## 1. Why These Two Exist

**SQLite (2000)** was built to work without a server — no admin, no daemon, no network. The whole database is one file your app opens with normal read/write calls. The goal was *zero configuration, zero setup*. Today it's the most widely deployed database in the world because it ships inside other software (browsers, phones, apps) rather than running as a separate service.

**PostgreSQL (1986 → SQL support 1996)** was built for many users hitting it at the same time. It runs as a server, handles concurrent reads and writes, and keeps data safe across crashes. The goal was *correctness and concurrency at scale*.

| | SQLite | PostgreSQL |
|---|---|---|
| Core constraint | No server, no admin | Many users, must not corrupt each other |
| Optimizes for | Simplicity, portability | Concurrency, durability, complex queries |
| Accepts the cost of | Weak multi-writer support | Operational complexity (server, tuning, vacuum) |

Everything below follows from this: **embedded library vs. client-server**.

---

## 2. Architecture

### SQLite — embedded model

No separate process. Your app links SQLite as a library and calls it directly. SQLite reads/writes one file on disk.

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
                                          │ read()/write()/fsync()
                                          ▼
                              ┌───────────────────────┐
                              │   single .db file     │
                              │  + -wal / -journal    │
                              └───────────────────────┘
```

Query path: SQL → VDBE bytecode → B-tree layer → pager fetches 4 KB pages → rows returned. No network, no second process.

### PostgreSQL — client-server model

A client connects over a socket. A `postmaster` daemon forks a backend process per connection. All backends share a memory pool (`shared_buffers`) and a set of background workers.

```
   client ──TCP/unix socket──►  postmaster
                                    │ forks one backend per connection
        ┌───────────────────────────┼───────────────┐
        ▼                           ▼                ▼
   backend #1                  backend #2        backend #N
        │                           │                │
        └──────────┬────────────────┴───────┬────────┘
                   ▼                         ▼
        ┌─────────────────────┐   ┌──────────────────────────┐
        │   shared_buffers    │   │  WAL writer, checkpointer │
        │  (shared page cache)│   │  autovacuum, bgwriter     │
        └──────────┬──────────┘   └──────────────────────────┘
                   ▼
   base/<oid>/ heap + index files  +  pg_wal/
```

Query path: SQL over socket → parse → cost-based planner picks a plan → executor pulls pages through `shared_buffers` → changes written to WAL first → result sent back over socket.

### Side by side

| Component | SQLite | PostgreSQL |
|---|---|---|
| Process model | In-process library | Daemon + one backend per connection |
| Data lives in | One `.db` file | A directory of many files |
| Shared memory | None | `shared_buffers` shared across backends |
| Background workers | None | WAL writer, checkpointer, autovacuum |
| Client transport | Function call | TCP / Unix socket |

---

## 3. Internal Design

### On-disk layout

**SQLite = one file = pages laid end to end.** From my Lab 4 hex walk of `pokedex.db` (page size 4096 B, 6 pages):

```
offset  0          4096        8192       12288      16384      20480      24576
        ├──Page 1───┼──Page 2───┼──Page 3──┼──Page 4──┼──Page 5──┼──Page 6──┤
        │ file hdr  │ pokemon   │  leaf    │  leaf    │  leaf    │  leaf    │
        │ + schema  │ INTERIOR  │ rowids   │ rowids   │ rowids   │ rowids   │
        │ b-tree    │ (root)    │ 1..32    │ 33..64   │ 65..97   │ 98..100  │
        └───────────┴───────────┴──────────┴──────────┴──────────┴──────────┘
```

The first 100 bytes are a file header starting with the magic string `"SQLite format 3\0"`. Every table is a B-tree of pages; the schema itself is just another B-tree on page 1.

**PostgreSQL = a directory of files.** Each table and index is one or more heap files under `base/<db-oid>/<relation-oid>`, split at 1 GB, each made of 8 KB pages. WAL lives in `pg_wal/` separately.

| | SQLite | PostgreSQL |
|---|---|---|
| Page size | 4 KB | 8 KB |
| Files per table | 0 (shared single file) | ≥ 1 heap + index files + WAL |
| Schema storage | `sqlite_schema` B-tree on page 1 | system catalogs (`pg_class`, etc.) |

### Slotted page layout

Both engines use the same page structure: a header at the top, a cell-pointer array growing down, and row content growing up from the bottom. The gap in the middle is free space. From my Lab 4 walk:

```
offset 0                                                            offset 4095
├─────────┬───────────────┬──────────────────────────────┬───────────────┤
│ Page    │ Cell pointer  │                              │  Cell content │
│ header  │ array         │   ← free space →             │  (grows back  │
│ 8/12 B  │ N × 2 bytes   │                              │   to middle)  │
└─────────┴───────────────┴──────────────────────────────┴───────────────┘
```

PostgreSQL uses the same idea. This is why studying SQLite's raw bytes also teaches you PostgreSQL's page layout.

### Indexes

Both default to B-trees, but they relate to rows differently:

- **SQLite:** the table *is* a B-tree keyed on `rowid`. `INTEGER PRIMARY KEY` is just an alias for rowid, so a primary key lookup is the table walk itself. Interior cells in my Lab 4 DB were 5 bytes each (4-byte child pointer + 1-byte rowid varint), so the tree stays very shallow — 100 rows needed only 3 page reads.
- **PostgreSQL:** the table is an unordered **heap**. Indexes are separate B-tree files that point at heap rows by `(page, offset)`. A primary key is a unique index sitting beside the heap, not replacing it.

```
SQLite:   PK  ==  the table B-tree          (index-organized)
Postgres: heap (unordered)  +  nbtree index ──TID──► heap tuple
```

### Example lookup (Lab 4)

`SELECT * FROM pokemon WHERE id = 42` on the SQLite DB:

1. Read **page 1** → `sqlite_schema` says pokemon root is page 2.
2. Read **page 2** (interior, type `0x05`). Keys are 32, 64, 97. `42 > 32` and `42 ≤ 64` → go to page 4.
3. Read **page 4** (leaf, type `0x0D`). Binary-search for rowid 42, decode the record, return it.

Three page reads total. PostgreSQL does the equivalent nbtree walk, then one extra fetch into the heap.

### Concurrency

This is the biggest difference.

**SQLite** uses a **WAL file** (or rollback journal in legacy mode). WAL lets readers keep reading while one writer appends. But the hard limit is: **one writer at a time for the whole database**. Writes take a database-level lock. No per-row locking, no parallel writes.

**PostgreSQL** uses **MVCC**. Every row version carries two hidden columns — `xmin` (which transaction created it) and `xmax` (which transaction deleted/replaced it). An `UPDATE` doesn't overwrite — it writes a new row version and marks the old one's `xmax`. Each transaction sees a snapshot: a row is visible if its `xmin` committed before the snapshot and its `xmax` hasn't. The result: **readers never block writers, writers never block readers**. (This is exactly what I implemented by hand in Lab 8.)

| | SQLite | PostgreSQL |
|---|---|---|
| Concurrency model | DB-level lock, one writer | MVCC, many concurrent readers + writers |
| Update strategy | In-place (journal/WAL for rollback) | Append new row version, mark old `xmax` |
| Cleanup | n/a | `VACUUM` reclaims dead row versions |

The cost of MVCC: old row versions pile up and need `VACUUM` to clean them. You can see this in the file sizes in §5.

### Durability

**SQLite:** before changing a page it copies the original to a journal (or appends to WAL), then `fsync()`s at commit. Crash recovery replays or rolls back from the journal/WAL.

**PostgreSQL:** uses WAL as a first-class subsystem. The rule: **write the WAL record before changing the data page.** On `COMMIT`, WAL is `fsync()`ed — that flush *is* the durability guarantee, even if the heap page is still dirty in `shared_buffers`. A checkpoint later flushes dirty pages and lets old WAL be recycled. After a crash, PostgreSQL replays WAL from the last checkpoint forward. WAL also powers replication and point-in-time recovery.

```
change → WAL record written → WAL fsync at COMMIT  ← data is safe here
                                   │
         checkpoint flushes heap pages, old WAL recycled
         crash recovery: replay WAL from last checkpoint
```

### Memory

- **SQLite:** per-connection page cache. Optional `mmap_size` to map the file directly into process memory and skip the `read()` copy.
- **PostgreSQL:** one shared `shared_buffers` pool used by all backends, with a background writer that trickles dirty pages out so checkpoints don't cause a big stall.

---

## 4. Trade-offs

### SQLite

**Good for:**
- Zero config, zero ops — no server to run or tune.
- Whole database is one portable file.
- No network overhead; simple queries are very fast (see Q1 below).
- Tiny footprint — phones, browsers, embedded devices.

**Not good for:**
- Busy multi-user apps — one writer at a time is a hard limit.
- Complex joins — no cost-based planner or parallelism, so big joins can time out (see Q3 below).
- No built-in users, roles, or network access.

### PostgreSQL

**Good for:**
- Real concurrency — readers and writers don't block each other.
- Complex queries — the cost-based parallel planner wins decisively on large joins.
- Strong durability, replication, and point-in-time recovery via WAL.
- Rich features: roles, extensions, partitioning, custom types.

**Not good for:**
- Operationally heavy — a server to run, connections to manage, parameters to tune.
- MVCC creates dead tuples; neglecting `VACUUM` causes bloat.
- Per-connection backend processes are heavy (hence connection poolers like PgBouncer).

**In one line:** SQLite trades concurrency for simplicity. PostgreSQL trades simplicity for concurrency and scale. Neither is universally better — it depends on the use case.

---

## 5. Experiments

All numbers are from my **Lab 2 benchmark** — same schema, same data, same queries across PostgreSQL, MySQL, and SQLite.

**Dataset:** `customers` = 100,000 rows · `orders` = 500,000 rows · `order_items` = 1,500,873 rows. Indexes on join/filter columns.

### Query timings

| Query | PostgreSQL | SQLite | MySQL |
|---|---|---|---|
| Q1 — filter + aggregate on `orders` | **31.6 ms** | 111 ms | 733 ms |
| Q2 — join customers × orders, group by city | **71.9 ms** | 734 ms | 1,047 ms |
| Q3 — join orders × order_items (500k × 1.5M), top 10 | **549 ms** | **> 20 min (killed)** | 3,090 ms |

### Storage

| | PostgreSQL | SQLite |
|---|---|---|
| DB size | 260 MB | **197 MB** |
| Page size | 8 KB | 4 KB |
| Memory knob | `shared_buffers` | `mmap_size` / OS page cache |

### `mmap_size` experiment (SQLite)

Compared `PRAGMA mmap_size = 0` vs `268435456` (256 MB):

| Query | mmap=0 | mmap=256MB | Change |
|---|---|---|---|
| Q1 | 111 ms | 113 ms | none |
| Q2 | 734 ms | 744 ms | none |

### What the numbers show

- **SQLite is fast on simple queries (Q1: 111 ms vs MySQL's 733 ms).** No server, no network — a single-table aggregate runs entirely in-process. Best case for the embedded model.
- **SQLite can't finish the big join (Q3 timed out).** No hash join, no parallelism, no cost-based optimizer → 500k × 1.5M degenerates into a nested-loop scan. This is architectural, not a bug.
- **PostgreSQL is fastest on every query**, and the gap grows with complexity (≈3.5× faster than SQLite on Q1, ≈10× on Q2, and SQLite can't even finish Q3).
- **SQLite's file is smaller (197 MB vs 260 MB)** on identical data because PostgreSQL stores `xmin`/`xmax` visibility info and dead tuple versions — the direct cost of MVCC.
- **`mmap_size` did nothing on macOS.** macOS already keeps file pages warm in the OS page cache, so skipping the `read()` copy saves nothing when data is already cached. On Linux with a cold cache it can help — the result is platform-dependent.

### On-disk verification (Lab 4)

Hex-dumped a real SQLite DB and confirmed: the `"SQLite format 3"` magic header, 4096-byte page size at offset 16, interior page type byte `0x05` with split keys 32/64/97, four leaf pages (`0x0D`), and `32 + 32 + 33 + 3 = 100` rows total — matching the single-file, B-tree-of-pages design exactly.

---

## 6. Key Learnings

1. **One choice explains everything.** Embedded vs. client-server is the root cause of the file layout, concurrency, durability, and benchmark differences. If you remember one thing, remember that.

2. **Concurrency is the real dividing line.** SQLite's single-writer lock vs. PostgreSQL's MVCC (`xmin`/`xmax` snapshots) is what decides whether a database can serve many users simultaneously.

3. **MVCC isn't free.** PostgreSQL's larger file (260 MB vs 197 MB on identical data) is the cost of keeping multiple row versions. Concurrency costs storage and `VACUUM`.

4. **"Fastest" depends on the workload.** SQLite beat MySQL on Q1 but timed out on Q3. PostgreSQL finished Q3 in 549 ms. Use SQLite for local/embedded/zero-ops workloads; PostgreSQL for multi-user, join-heavy, production workloads.

5. **`mmap_size` did nothing on macOS.** An optimization only helps if it removes an actual bottleneck. The OS page cache had already warmed the data, so there was nothing left to shortcut.

6. **Both engines share the same core ideas.** Slotted pages, B-trees, and write-ahead logging appear in both. Reading SQLite's raw bytes in Lab 4 taught me PostgreSQL's page model too — the concepts transfer directly.

---

## References

- D.R. Hipp et al., *SQLite Database File Format* — https://www.sqlite.org/fileformat.html
- *SQLite — Write-Ahead Logging* — https://www.sqlite.org/wal.html
- PostgreSQL Documentation — *Internals: Database Page Layout, MVCC, WAL* — https://www.postgresql.org/docs/current/
- Alex Petrov, *Database Internals* (O'Reilly, 2019)
- My lab work: Lab 1 (file I/O & the `read()`/page-cache path), Lab 2 (benchmark), Lab 4 (hex-walking SQLite pages), Lab 8 (hand-built MVCC + two-phase locking).
