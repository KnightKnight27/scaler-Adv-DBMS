# PostgreSQL vs SQLite — Architecture Comparison

**Piyush Bansal — 24BCS10079**

---

## 1. Problem Background

Both PostgreSQL and SQLite are relational databases that speak SQL, but they were
built to solve **two very different problems**, and almost every difference between
them comes from that one decision.

- **SQLite** was built to be a database *inside* an application — no separate
  server, no setup, the whole database is just one file on disk. The goal was
  "a database that needs zero administration and can ship inside any program."
- **PostgreSQL** was built to be a *shared* database that many users and
  applications connect to at the same time, over a network, with strong
  guarantees about correctness even under heavy concurrent load.

So the simplest way to remember it:
> **SQLite is a library. PostgreSQL is a server.**

---

## 2. Architecture Overview

### SQLite — Embedded (in-process)

```
   Your Application Process
  ┌───────────────────────────┐
  │   App code                │
  │   ───────────             │
  │   SQLite library (linked) │   <- SQLite runs INSIDE your app
  │   ───────────             │
  └────────────┬──────────────┘
               │ direct file read/write
               ▼
        database.db  (one file on disk)
```

There is no server. When your app calls SQLite, it directly reads and writes the
`.db` file. The "database" is just function calls into a linked library.

### PostgreSQL — Client-Server (process-per-connection)

```
  Client 1 ─┐
  Client 2 ─┤  (network: TCP / sockets)
  Client 3 ─┘
            │
            ▼
   ┌──────────────────────────────────┐
   │  PostgreSQL Server (postmaster)   │
   │  ┌──────────┐  ┌──────────┐       │
   │  │ backend  │  │ backend  │  ...  │  <- one process per client
   │  └──────────┘  └──────────┘       │
   │  Shared Buffers (shared memory)   │
   │  WAL writer · Checkpointer · etc. │
   └────────────────┬─────────────────┘
                    ▼
              data files on disk
```

A central server process (`postmaster`) listens for connections and spawns a
**separate backend process per client**. All backends share a common memory area
(shared buffers) so they coordinate safely.

---

## 3. Internal Design

| Area | SQLite | PostgreSQL |
|------|--------|-----------|
| **Process model** | In-process library, no server | Client-server, one process per connection |
| **Storage** | Single `.db` file (B-tree pages) | A directory of many files (tables, indexes, WAL) |
| **Page layout** | B-tree pages, default 4 KB | Heap + index pages, default 8 KB |
| **Indexes** | B-tree | B-tree (+ Hash, GiST, GIN, BRIN…) |
| **Concurrency** | One writer at a time (file lock / WAL mode) | MVCC — many readers + writers at once |
| **Transactions** | ACID via rollback journal or WAL | ACID via MVCC + WAL |
| **Types** | Dynamic typing (type per value) | Strict static typing (type per column) |

**Storage:** SQLite keeps *everything* — tables, indexes, schema — in one file as
a tree of pages. PostgreSQL spreads data across many files in a data directory,
plus a separate Write-Ahead Log.

**Concurrency is the biggest internal difference:**
- SQLite allows **one writer at a time**. A write locks the database file; other
  writers wait. (WAL mode lets readers continue during a write, but still only one
  writer.) This is fine for one app, bad for hundreds of users.
- PostgreSQL uses **MVCC** (Multi-Version Concurrency Control): each update creates
  a *new version* of the row, so readers never block writers and writers never
  block readers. This is what lets many users work at once.

---

## 4. Design Trade-Offs

### SQLite
**Advantages**
- Zero configuration, zero server to manage.
- Extremely lightweight and fast for a single user.
- The whole database is one portable file — easy to copy, ship, embed.

**Limitations**
- Only one writer at a time → poor for high-concurrency multi-user systems.
- No network access — the app must be on the same machine as the file.
- Fewer advanced features (no stored procedures, limited types, no real user roles).

### PostgreSQL
**Advantages**
- Handles many concurrent users safely (MVCC).
- Rich features: complex types, advanced indexes, extensions, strong constraints.
- Scales well for large, multi-user, write-heavy systems.

**Limitations**
- Needs a running server + administration.
- Heavier — overkill for a small app or a phone.
- Process-per-connection can get expensive at thousands of connections (hence
  connection poolers like PgBouncer).

**The core trade-off:** SQLite trades concurrency and features for *simplicity*;
PostgreSQL trades simplicity for *concurrency, scale, and correctness under load*.

---

## 5. Experiments / Observations

A simple way to see the difference yourself:

```sql
-- SQLite: check the page size of a database file
PRAGMA page_size;      -- typically 4096

-- PostgreSQL: check the block (page) size
SHOW block_size;       -- typically 8192
```

**Concurrency test (the clearest demo):**
- Open two SQLite connections to the same file and start a write transaction in
  both. The second `INSERT` will fail or block with `database is locked` — proving
  the single-writer rule.
- Do the same with two PostgreSQL clients writing *different* rows — both succeed
  concurrently, because MVCC gives each its own row versions.

**Observation:** SQLite's whole behavior follows from "it's a file"; PostgreSQL's
whole behavior follows from "it's a shared server."

---

## 6. Key Learnings

- The single design choice — **embedded library vs client-server** — explains
  almost every other difference between the two.
- **Why SQLite is great for mobile apps:** one file, no server, no admin, and a
  phone app has only one user writing at a time — so the single-writer limit never
  hurts. It's literally built into Android, iOS, and browsers.
- **Why PostgreSQL is preferred for large multi-user systems:** MVCC + client-server
  means hundreds of users can read and write at once without corrupting each other's
  data.
- A "better" database doesn't exist in the abstract — it depends on whether you need
  *simplicity* (SQLite) or *concurrency and scale* (PostgreSQL).

---

### References
- SQLite documentation — *Architecture of SQLite* (sqlite.org/arch.html)
- PostgreSQL documentation — *Internals → Overview* and *Concurrency Control (MVCC)*
