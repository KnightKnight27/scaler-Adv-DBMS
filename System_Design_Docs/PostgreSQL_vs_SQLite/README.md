# PostgreSQL vs SQLite — Two Very Different Answers to "Where Do I Put My Data?"

**Name:** Parth Sankhla
**Roll Number:** 24BCS10229

When I started this I assumed the comparison would be "big database vs small database." It isn't. SQLite and PostgreSQL are both relational, both speak SQL, both store rows in B-trees — and yet almost every design choice they make goes in the opposite direction. The reason is one decision made very early on: *does the database run inside my program, or as a separate program I talk to over a connection?* Everything else follows from that.

This write-up is me trying to explain that to myself, using the numbers I actually measured back in Lab 2 and the raw bytes I stared at in Lab 4.

---

## 1. Problem Background

The two systems were built for problems that barely overlap.

**SQLite** came from a need to store structured data *without* an administrator anywhere in sight. Richard Hipp wrote it around 2000 for a setting where you couldn't assume a running database server — the database had to be a plain file the program opened directly. So the goal became: zero configuration, no server process, no network, the whole database in a single file you can copy with `cp`. That is why today it sits inside your phone, your browser, your text editor's settings. It isn't a "small Postgres," it's a different category of thing — a library that happens to understand SQL.

**PostgreSQL** has the opposite ancestry. It grew out of the POSTGRES project at Berkeley in the mid-80s, where the whole point was a serious multi-user database server — many clients hitting the same data at the same time, needing isolation, durability, and an extensible type system. So it was a server from day one: a long-running process that owns the data and hands out connections.

So the background question each one answers is different. SQLite answers "how do I give one program reliable structured storage with no setup?" Postgres answers "how do I let many programs safely share the same data?"

---

## 2. Architecture Overview

The clearest way I can draw the difference is by where the database code actually runs.

**SQLite — the database is a library in my process:**

```
   +-----------------------------------+
   |        my application             |
   |   +---------------------------+   |
   |   |   SQLite (linked in)      |   |
   |   |   SQL -> bytecode (VDBE)  |   |
   |   |   B-tree pager            |   |
   |   +-----------+---------------+   |
   +---------------|-------------------+
                   | read()/write()/fsync()
                   v
          +------------------+
          |  sample.db file  |   <- one file, the whole database
          +------------------+
```

There is no second process. My program calls a function, SQLite parses the SQL, runs it as bytecode, reads and writes pages of the one file, and returns. The "server" is just function calls.

**PostgreSQL — the database is a separate server I connect to:**

```
   my app  --(libpq, TCP/socket)-->  postmaster
                                        |  fork()
                                        v
                              +-------------------+
                              | backend process   |  (one per connection)
                              +---------+---------+
                                        |
            shared memory  +------------v------------+
            (shared_buffers,| buffer pool, locks, WAL|
             WAL buffers)   +------------+------------+
                                        |
         background processes:          v
         checkpointer, bgwriter,   data files on disk
         walwriter, autovacuum     (one file per relation)
```

Every connection gets its own backend process, and they all coordinate through a shared-memory region plus a set of background helpers. When I ran `ps aux | grep postgres` in Lab 2, I could literally see them: the postmaster, the checkpointer, the background writer, the walwriter, the autovacuum launcher, plus io workers. SQLite, by comparison, showed up as *part of my own process* and nothing else.

That picture — one file vs a fleet of processes around shared memory — is the whole comparison in miniature.

---

## 3. Internal Design

### Where the bytes live

SQLite keeps **everything in one file**: every table, every index, the schema, and the free list, all as fixed-size pages inside `sample.db`. In Lab 2 I confirmed this the satisfying way — `page_size` was 4096 and `page_count` was 9882, and `4096 * 9882 = 40,476,672`, which matched the file size on disk almost exactly. The whole database really is just `page_size * page_count` bytes.

Postgres spreads out instead. **Each relation gets its own file**, and alongside the heap file there's a free space map (`_fsm`) and a visibility map (`_vm`). In Lab 2 the `users` heap was its own 26 MB file, the primary key index another file, the email index another, each with its `_fsm`/`_vm` siblings. Its block size is 8 KiB, fixed at compile time — twice SQLite's default.

### Page layout

Both use a slotted-page idea, which I got to see for real in Lab 4 when I dumped a SQLite file with `xxd`. The file started with the literal text `SQLite format 3`, and each page had a small header, then an array of cell pointers near the top, with the actual records (cells) packed in from the bottom of the page upward. The pointers grow down, the data grows up, and they meet in the middle — that's the free space. Postgres pages are the same spirit: a page header, then an array of line pointers (item IDs), then tuples filling in from the end.

### Tables and indexes

This is where they genuinely differ. In SQLite a normal table *is* a B-tree keyed by the integer rowid, and indexes are separate B-trees pointing back by rowid. Postgres stores table rows in an unordered **heap** and keeps every index as a separate structure that points into the heap by physical location (ctid). So in Postgres the table and its indexes are always separate objects — there's no "the table is the index" by default.

### Concurrency and durability

SQLite's concurrency is coarse because there's no server to arbitrate — it relies on file locks. In its default journal mode only one writer can be active at a time, and writers block readers; WAL mode loosens this so readers and a single writer can coexist. Durability comes from either a rollback journal or a write-ahead log plus `fsync`.

Postgres uses **MVCC**: a writer doesn't overwrite a row, it creates a new version, so readers never block writers and writers never block readers. Durability rides on its own WAL — the change is logged before the data page is written — with a checkpointer flushing dirty pages periodically. (I dug into MVCC, the buffer manager, and WAL much more in my PostgreSQL Internals write-up; here I just want the contrast.)

---

## 4. Design Trade-Offs

The honest summary is that neither is "better" — they paid for different things.

**SQLite's bargain.** By giving up the server it gets simplicity that's almost unfair: nothing to install, nothing to administer, no ports, no users, a database you can email to someone. The cost is concurrency and reach. One writer at a time is fine for one app on one device and miserable for fifty clients hammering the same table. And there's no network story at all — if two machines need the data, SQLite isn't the tool.

**PostgreSQL's bargain.** By committing to the server it gets real multi-user concurrency, strong isolation, replication, extensibility, and an enormous feature set. The cost is everything that fleet of processes implies: memory, a real installation, tuning knobs, and a non-trivial per-query fixed cost that I could measure (see below). For a single-user embedded use case that overhead is pure waste.

The performance implication that surprised me most: a lot of Postgres's measured "slowness" on small queries isn't query work at all, it's the price of being a server — connection setup, the libpq round-trip, parsing, planning, protocol encoding. SQLite skips all of that because it's just a function call. So on a tiny workload SQLite looks dramatically faster, but that gap is mostly fixed cost, not better algorithms.

---

## 5. Experiments / Observations

These are the numbers from my own Lab 2, where I loaded the same `users` table (200,000 rows, an index on `email`) into both engines and timed identical queries. I dropped the OS page cache between cold runs so the timings reflected real I/O.

**Sizes and pages**

| | SQLite | PostgreSQL |
|---|---|---|
| default page / block | 4096 B | 8192 B |
| pages for the data | 9882 (whole DB, one file) | 3364 heap + 551 PK idx + 995 email idx |
| files on disk | 1 | 1 per relation, plus `_fsm` / `_vm` |
| `bytes = page_size * page_count` check | held exactly | held exactly |

**Query timings (median of 3 runs, seconds)**

| query | sqlite cold | sqlite warm | postgres cold | postgres warm |
|---|---:|---:|---:|---:|
| full scan: `COUNT(*), SUM(LENGTH(bio))` | 41.7 ms | 29.4 ms | 103.3 ms | 72.2 ms |
| indexed: `WHERE email = '...'` | 9.0 ms | 3.0 ms | 67.5 ms | 40.6 ms |

The interesting part is what Postgres's own `EXPLAIN (ANALYZE, BUFFERS)` said about that same indexed lookup:

```
Index Scan using idx_users_email on users
  (actual time=0.023..0.024 rows=1)
  Buffers: shared hit=4
Execution Time: 0.105 ms
```

So Postgres did the actual lookup in **0.1 ms** — it touched exactly 4 pages (three B-tree levels plus one heap page) — but the end-to-end time from `psql` was ~40 ms warm. That ~40 ms gap is the server tax: process startup, IPC, parse, plan, MVCC visibility, wire format. That single observation reframed the whole comparison for me.

Two more things I noticed:
- Postgres **parallelised the full scan on its own** with two workers, without me asking. On this small table that didn't help much, but it hints at how the comparison flips on bigger data and bigger machines.
- I also tested SQLite's `PRAGMA mmap_size`. Turning it up to 256 MiB pushed the process VSZ from ~9 MB to ~46 MB (the whole file gets mapped in), but the query times stayed inside the run-to-run noise. mmap only earns its keep when the database is much larger than the buffer pool *and* you do lots of random access — a 39 MB file on an NVMe SSD is the wrong test for it.

And from Lab 4, the byte-level view: dumping a small SQLite database with `xxd -g 1 -c 16` showed the `SQLite format 3` magic string at offset 0 and let me walk the cell pointer array of a B-tree page by hand. Seeing the page layout as actual hex made the "everything is pages in one file" claim concrete instead of abstract.

---

## 6. Key Learnings

- **One decision drives the rest.** In-process library vs client-server isn't one difference among many — it's the root that explains the file layout, the concurrency model, the process model, and even the performance shape.
- **"Faster" is a trap question.** SQLite beat Postgres on every query I timed, but mostly because it has no protocol to pay for. Postgres's per-query overhead is the cost of being a safe, concurrent, networked server — and `EXPLAIN ANALYZE` showed the real query work was a fraction of a millisecond.
- **Their storage philosophies are mirror images.** SQLite: one file, table-is-a-B-tree. Postgres: a file per relation, heap plus separate indexes. Both slotted pages underneath, which I'd already seen in the hex dump.
- **Match the tool to the sharing model.** If one program needs reliable local storage, SQLite's simplicity is a feature, not a limitation. The moment many clients need to share and isolate, the server architecture I was "paying" for becomes the entire point.
