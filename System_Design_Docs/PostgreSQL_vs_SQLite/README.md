# PostgreSQL vs SQLite — Architecture Comparison

> Advanced DBMS — System Design Discussion  
> Roll No: 10075 | Nase Anishka

---

## 1. Problem Background

PostgreSQL and SQLite are both relational databases that speak SQL, but they were built for completely different situations. The easiest way to understand the difference is to ask: *who is the database serving, and where does it run?*

PostgreSQL was built for the case where a database is a shared resource that many different applications and users access at the same time, over a network. Think of a web app with thousands of users hitting the same tables. It needs to handle concurrent reads and writes correctly, recover cleanly after crashes, and enforce access control across users. PostgreSQL was designed precisely for this — it's been around since 1986 (originally from UC Berkeley) and is used heavily in production by companies that need reliability and scale.

SQLite is the opposite. It's an embedded database — there's no server at all. The entire database lives in a single file, and you just open it from your application like any other file. SQLite's own documentation says it best: *"SQLite does not compete with client/server databases. SQLite competes with fopen()."* It's not trying to be PostgreSQL. It's trying to replace the custom binary file format that most apps would otherwise write by hand. That's why it's in every phone, every browser, and basically every app that needs local data storage.

These two different goals explain everything else about how they're built.

---

## 2. Architecture Overview

### PostgreSQL

```
  Client Apps (psql / Django / JDBC …)
        |
        |  TCP or Unix socket
        v
  +------------------+
  |   Postmaster     |  ← the main process, listens for connections
  |  (supervisor)    |     forks a new backend for each connection
  +------------------+
     |        |        |
     v        v        v
  Backend  Backend  Backend   ← one OS process per connection
     \        |        /
      \       |       /
       v      v      v
    +----------------------+
    |    Shared Memory      |
    |  - shared_buffers     |  ← 8KB page cache, shared by all backends
    |  - WAL buffers        |
    |  - lock tables        |
    +----------+-----------+
               |
       +-------+-------+
       |               |
  Heap/Index files   pg_wal/
  (the actual data)  (write-ahead log)

  background helpers: bgwriter, autovacuum, checkpointer
```

Each connection gets its own backend process. All backends share the same memory pool (`shared_buffers`) so a page one backend reads stays warm for others. All changes are written to the WAL before being committed — this is how crash recovery works.

### SQLite

```
  Your Application
        |
        | (library call, no network)
        v
  +------------------------------------------+
  |         SQLite Library (sqlite3.c)        |
  |                                           |
  |  SQL → Tokenizer → Parser → Code Gen      |
  |                   |                       |
  |               VDBE (bytecode VM)          |
  |                   |                       |
  |           B-Tree Module                   |  ← tables and indexes
  |                   |                       |
  |           Pager   |                       |  ← page cache + locking
  |                   |                       |
  |           VFS (OS file operations)        |
  +------------------+------------------------+
                     |
              mydb.db  (one file = everything)
```

No server. No daemon. No config files. The entire database engine is a single C library compiled into your app. Queries go through a small bytecode interpreter (VDBE), which talks to a B-tree module, which talks to the pager, which talks to the OS. All of this happens in-process with no network involved.

---

## 3. Internal Design

### How data is stored

In PostgreSQL, a table is stored as a **heap file** — rows are just written wherever there's free space on a page. The pages are 8KB each. There's no guaranteed ordering of rows in the file. Indexes are separate B-tree files that point back into the heap.

In SQLite, **everything is a B-tree inside one file**. The table itself is a B-tree (keyed by rowid, with the full row in the leaf). Each index is another B-tree. The whole database is just a collection of B-trees packed into a single file.

You can actually see this in SQLite:
```sql
SELECT type, name, rootpage FROM sqlite_schema;
-- table  users    2
-- index  idx_email  3
-- Two B-trees, two different rootpages, one file
```

### Concurrency — the biggest practical difference

This is where the two databases are most different.

**PostgreSQL uses MVCC** (Multi-Version Concurrency Control). When you update a row, PostgreSQL doesn't overwrite it — it writes a new version of the row and marks the old one as deleted. Every transaction gets a consistent snapshot of the data at the time it started. Readers never block writers. Writers never block readers. Multiple transactions can read and write to the same table at the same time. The downside is that old row versions accumulate and need to be cleaned up by a process called VACUUM.

Each row stores hidden fields — `xmin` (which transaction created it) and `xmax` (which transaction deleted it). Based on these, PostgreSQL decides which version of a row each transaction should see.

**SQLite uses file-level locking**. There's only ever one writer at a time. The lock progression is:
- SHARED (reading — multiple processes can hold this)
- RESERVED (planning to write — only one allowed)
- EXCLUSIVE (actually writing — everyone else waits)

In WAL mode (`PRAGMA journal_mode=WAL`), readers and the single writer can overlap, but there's still just one writer. For most SQLite use cases (mobile apps, local storage) this is fine because write contention is rare.

### Crash recovery

PostgreSQL uses a Write-Ahead Log (WAL). Before any change hits the data file, it's first written to the WAL. If the server crashes, it replays the WAL on restart to recover committed changes.

SQLite uses a rollback journal by default — before modifying pages, it saves the originals to a `-journal` file. On commit, the journal is deleted. If there's a crash, the journal is detected on the next open and the changes are rolled back. In WAL mode, new changes go to a `-wal` file and are periodically copied back to the main file.

---

## 4. Design Trade-Offs

| | PostgreSQL | SQLite |
|--|--|--|
| Deployment | Needs a running server | Just a file |
| Concurrent writes | Many writers (MVCC) | One writer at a time |
| Remote access | Yes, over TCP | No — same process only |
| Permissions | Full role-based access control | OS file permissions only |
| Dead row cleanup | VACUUM (needed because of MVCC) | Not needed |
| Resource usage | ~5–10 MB per connection, needs RAM | ~1 MB library, near zero overhead |
| Scalability | Can scale to thousands of connections, replication | Single machine, single file |

The biggest trade-off with PostgreSQL's MVCC is the VACUUM problem. Because updates write new versions rather than overwriting in place, dead old versions pile up. If VACUUM can't run (e.g., a long-running transaction pins old snapshots), tables bloat and queries slow down. SQLite doesn't have this problem — updates happen in place and there are no dead versions to clean.

The biggest trade-off with SQLite is the single-writer limit. For any app with many concurrent write users, SQLite just doesn't work. PostgreSQL's MVCC means two users writing to different rows of the same table don't block each other at all.

SQLite wins in zero-admin simplicity. The database file is self-contained, easy to copy, easy to backup, and works on any machine without setup. This is why it's the most deployed database in the world by a huge margin — it ships in every Android phone, every iPhone, and every browser.

---

## 5. Experiments / Observations

**SQLite file structure is literally just B-trees.**
I ran `PRAGMA page_count` on a small test database and got 4 pages (16KB total). The `sqlite_schema` table showed each table and index mapped to its own `rootpage`. Three B-trees, one file, 16KB. That's the whole database.

**VDBE bytecode is visible.**
Running `EXPLAIN SELECT * FROM users WHERE email = ?` in SQLite shows the actual virtual machine opcodes — `SeekGE`, `IdxGT`, etc. You can see exactly how it walks the index B-tree first, then uses the rowid to look up the table B-tree. Two tree traversals for one indexed query.

**PostgreSQL WAL is always writing.**
Even a simple UPDATE generates WAL records. After a checkpoint, the first write to any page writes the *entire 8KB page* into the WAL (called a Full Page Image) to protect against partial writes on crash. This is why WAL can be surprisingly large relative to the actual data being changed.

**SQLite locking in action.**
If you open two SQLite connections and have one do `BEGIN; INSERT ...`, the second connection trying to insert gets `SQLITE_BUSY` immediately. In PostgreSQL, two sessions inserting different rows proceed with no conflict at all.

---

## 6. Key Learnings

1. **The server vs. no-server split drives everything else.** PostgreSQL pays the price of process isolation, shared memory, and WAL to serve many users correctly. SQLite avoids all of that by being a library and giving up multi-user concurrent write access. Neither is wrong — they're built for different jobs.

2. **MVCC is a trade with VACUUM.** PostgreSQL never overwrites rows — it writes new versions. This gives great concurrent read performance. The cost is that old versions accumulate and need a cleanup job (VACUUM). SQLite doesn't do MVCC, so it doesn't have this problem, but it can only have one writer.

3. **Both databases need two lookups for secondary index queries.** Whether it's "index → heap tuple ID → heap file" (PostgreSQL) or "index → rowid → table B-tree" (SQLite), the structure is the same. Covering indexes (where all needed columns are already in the index) avoid the second lookup in both.

4. **SQLite's simplicity is a genuine engineering advantage.** Zero config, zero server, zero admin. Copy the file = backup done. This isn't a compromise — it's a deliberate design choice that makes SQLite the right tool for apps where you need reliable local data storage without the complexity of a server.

5. **Crash safety works differently.** PostgreSQL's WAL is write-first, very robust, supports point-in-time recovery and streaming replication. SQLite's journal/WAL approach is simpler but equally crash-safe for a single-file, single-machine database. Both give you durability — just implemented very differently.

---

## References

- SQLite: [Architecture](https://www.sqlite.org/arch.html), [File Format](https://www.sqlite.org/fileformat2.html), [WAL Mode](https://www.sqlite.org/wal.html), [When To Use](https://www.sqlite.org/whentouse.html)
- PostgreSQL 16 Docs: [MVCC](https://www.postgresql.org/docs/current/mvcc.html), [WAL](https://www.postgresql.org/docs/current/wal.html), [Storage Layout](https://www.postgresql.org/docs/current/storage-file-layout.html)
