# PostgreSQL vs SQLite

- **Role Number:** 24BCS10345
- **Name:** Ansh Mahajan

Both speak SQL and both are relational, but they sit at opposite ends of the
deployment spectrum: SQLite is an **embedded library** that *is* your
application's process, while PostgreSQL is a **client-server** database that
runs as its own set of processes. This document explains the architectural
consequences of that one difference. It extends the Lab 2 benchmark (page sizes
and query timings) and the Lab 4 byte-level study of the SQLite file.

## 1. Process architecture

```
SQLite (embedded)                  PostgreSQL (client-server)
┌────────────────────┐            ┌───────────┐   TCP/socket   ┌──────────────────┐
│  application code   │            │  client    │ ─────────────► │ postmaster        │
│  ┌───────────────┐  │            │ (psql/app) │                │  ├ backend per    │
│  │ libsqlite3     │  │            └───────────┘                │  │  connection     │
│  │  (the engine)  │  │                                         │  ├ background      │
│  └──────┬────────┘  │                                         │  │  writer          │
└─────────┼───────────┘                                         │  ├ WAL writer       │
          ▼                                                      │  ├ checkpointer     │
   one .db file on disk                                          │  └ autovacuum       │
                                                                 └────────┬─────────┘
                                                                          ▼
                                                                  data dir (many files)
```

SQLite runs *in-process*: a function call, not a network round-trip. PostgreSQL
forks a **backend process per connection** and coordinates shared memory
(shared buffers) and background workers.

## 2. Storage on disk

| | SQLite | PostgreSQL |
| --- | --- | --- |
| Footprint | A **single file** (plus a transient `-journal` or `-wal`) | A **data directory** of many files (one+ per table/index) |
| Default page size | **4096 bytes** (verified below; see Lab 4) | **8192 bytes** (`block_size`, measured in Lab 2) |
| Table organization | Rowid B-tree (clustered on `rowid`); `WITHOUT ROWID` for index-organized | Unordered **heap** + separate indexes |
| Schema catalog | `sqlite_schema` table on page 1 (dissected in Lab 4) | `pg_catalog` system tables |

Verified locally with the SQLite CLI:

```text
$ sqlite3 demo.db "PRAGMA page_size; PRAGMA journal_mode;"
4096
delete            # default rollback journal
$ sqlite3 demo.db "PRAGMA journal_mode=WAL;"
wal               # WAL is opt-in per database
```

## 3. Concurrency model — the big practical difference

- **SQLite**: a database-file-level writer lock. In the default rollback-journal
  mode there is **one writer at a time** and writers block readers. **WAL mode**
  relaxes this so readers and a single writer can proceed concurrently, but it
  is still **one writer at a time** for the whole file.
- **PostgreSQL**: full **MVCC** (see the PostgreSQL_Internals doc) with
  row-level locking. Many writers and many readers run concurrently; readers
  never block writers. Built for high-concurrency multi-user workloads.

This is the deciding factor for most architecture choices: SQLite is superb for
single-writer / read-mostly / embedded scenarios; PostgreSQL is built for
concurrent multi-client systems.

## 4. Types and constraints

- **SQLite** uses *type affinity* and is dynamically typed — a column has a
  preferred type but will store other types (and, by default, does not enforce
  `VARCHAR` lengths). `STRICT` tables (added in 3.37) opt into rigid typing.
- **PostgreSQL** is strictly, statically typed with a very rich type system
  (arrays, `jsonb`, ranges, geometric, custom types) and full constraint
  enforcement, including deferrable constraints and rich `CHECK`s.

## 5. Feature surface

| Capability | SQLite | PostgreSQL |
| --- | --- | --- |
| Stored procedures / PL languages | No (functions via app/extensions) | Yes (PL/pgSQL, PL/Python, …) |
| Concurrent writers | One at a time | Many (MVCC) |
| Replication / HA | None built-in | Streaming + logical replication |
| Network access | None (in-process) | Yes (TCP) |
| Extensions / index types | Limited | Extensive (GIN, GiST, BRIN, …) |
| Footprint / setup | Zero-config, ~1 MB library | Server install + tuning |

## 6. When to pick which

**Reach for SQLite when:** the data lives with one app instance — mobile apps,
desktop apps, browsers, edge/IoT devices, CLI tools, test fixtures, on-disk
file formats, and read-mostly caches. Its whole value is that there is no server
to run.

**Reach for PostgreSQL when:** multiple clients write concurrently, you need
strong typing and constraints, replication/HA, large datasets, or advanced
features (JSON indexing, full-text search, GIS). It is the default for
multi-user backend systems.

## 7. Observations / takeaways

- "Same SQL" hides a deep architectural fork: **embedded vs server** drives
  everything else — concurrency, file layout, types, and operations.
- SQLite's single-writer model is a feature, not a bug: removing the server is
  precisely what makes it the most-deployed database in the world.
- The Lab 2 numbers (SQLite ~0.35–0.55 ms vs PostgreSQL ~0.80 ms on a tiny
  table) reflect this: with no client-server hop, SQLite wins on a small local
  query — but that advantage inverts the moment real concurrency is required.

## References

- *SQLite Documentation* — "Database File Format", "Write-Ahead Logging",
  "Appropriate Uses For SQLite".
- *PostgreSQL Documentation* — "Concurrency Control", "Server Administration".
- Lab 2 (SQLite vs PostgreSQL benchmark) and Lab 4 (SQLite file format) of this
  repository.
