# PostgreSQL vs SQLite - Architecture Comparison

I compared two SQL databases that made opposite design choices. PostgreSQL is a
client-server database for many users. SQLite is a small library that runs
inside one application. Both are good - they just solve different problems.

## 1. Problem Background

- PostgreSQL (from UC Berkeley, 1986) was built as a serious multi-user server:
  many clients over a network, strong correctness, rich SQL, long uptime.
- SQLite (2000) was built to *remove* the server. Most apps just need to store
  data on disk reliably, so SQLite puts the whole engine inside the app.

| | PostgreSQL | SQLite |
|---|---|---|
| Type | Client-server | Embedded library |
| Database is | Many files managed by a server | One single file |
| Made for | Many users at once | One app, mostly one writer |
| Network | Yes (TCP) | None |

## 2. Architecture Overview

```
PostgreSQL (client-server)         SQLite (embedded)
  clients -> network                 your app
       |                                |  function call
   Postmaster (forks 1 proc/client)     v
       |                            SQLite library (B-tree + pager)
   shared memory (shared_buffers)       |  file read/write
       |                                v
   data files + WAL                  one .db file
```

- PostgreSQL: a postmaster forks one backend process per client; all backends
  share one memory area (buffer cache). Background workers handle WAL,
  checkpoints, autovacuum.
- SQLite: no server, no process. SQL compiles to bytecode run by a small VM;
  "talking to the DB" is just a function call, not a network trip.

## 3. Internal Design

- **Storage.** PostgreSQL stores each table as a *heap* (rows placed anywhere)
  in 8 KB pages. SQLite stores everything (tables, indexes, schema) in one file
  of 4 KB pages, and each table is a B-tree keyed by rowid (clustered layout).
- **Indexes.** Both use B-trees. PostgreSQL indexes are separate and point to a
  physical row location; it also has Hash/GiST/GIN/BRIN. In SQLite the table
  *is* a B-tree, so an integer primary key needs no extra index.
- **Concurrency.** PostgreSQL uses MVCC: each row keeps xmin/xmax, an UPDATE
  writes a new row version, so readers never block writers. SQLite locks the
  whole file, so only one writer at a time (WAL mode lets readers run alongside
  that one writer).
- **Durability.** PostgreSQL writes a WAL record before the data page and
  replays it after a crash. SQLite uses a rollback journal (saves old pages) or
  WAL mode. I confirmed the file header is literally `SQLite format 3`, and WAL
  mode created side files `-wal` and `-shm`.

## 4. Design Trade-Offs

- PostgreSQL is client-server because many users need one shared consistent
  copy, a shared cache, and a global lock manager. Cost: you must run a server
  and every query is a network round trip.
- SQLite is embedded because one app on one device needs no server. Cost: no
  real multi-user write concurrency and no network access.
- PostgreSQL's "new version per update" gives great concurrency but creates
  dead rows, so it needs VACUUM. SQLite updates in place but locks out writers.
- Indexes are not free: on my test table, adding one index grew the file ~50%.

## 5. Experiments / Observations (run locally, SQLite 3.51.0)

I made `accounts(id INTEGER PRIMARY KEY, name, balance, city)` with 50,000 rows
and watched the planner.

- PK lookup: `SEARCH accounts USING INTEGER PRIMARY KEY (rowid=?)` - direct.
- Filter on un-indexed `city`: `SCAN accounts` - full table scan.
- After `CREATE INDEX idx_city`: `SEARCH accounts USING INDEX idx_city (city=?)`.
- Timing (50k rows): full scan ~0.004 s vs indexed ~0.000 s.
- File grew 1.52 MB -> 2.29 MB after one index. Header bytes = `SQLite format 3`.

So even tiny SQLite reasons about access paths like big PostgreSQL: `SCAN` means
read everything, `SEARCH USING INDEX` means use the B-tree to skip work.

## 6. Key Learnings

- The real split is the process model (server for many users vs library for one
  app), not the SQL.
- SQLite fits mobile/edge: no server, one portable file, local function calls.
- PostgreSQL fits large multi-user systems: client-server + MVCC let many
  clients read and write at once with strong correctness.
- MVCC is the heart of PostgreSQL concurrency, and VACUUM is the price for it.
- I saw indexes are a trade (faster reads, bigger files), and the planner makes
  that visible (`SCAN` vs `SEARCH`).

### References
PostgreSQL docs (Internals & Storage); SQLite docs (File Format, Architecture,
WAL); experiments run with `sqlite3` 3.51.0.
