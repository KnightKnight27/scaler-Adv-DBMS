# PostgreSQL vs SQLite — Architecture Comparison

**24BCS10404 — Rajveer Bishnoi**

> Two relational databases that both speak SQL and both store B-trees on disk, yet they were built for almost opposite worlds. PostgreSQL is a multi-process server you connect to over a socket; SQLite is a library you link into your program. Almost every architectural difference below falls out of that one decision.

All numbers in this document were measured locally on **PostgreSQL 18.3** and **SQLite 3.51** (macOS, Apple Silicon) using the same schema (`students`, `enrollments`) loaded with 20,000 and 200,000 rows respectively. The scripts to reproduce are in this folder (`sqlite_setup.sql`); the Postgres side reuses `../PostgreSQL_Internals/setup.sql` (identical schema).

---

## 1. Problem Background

**SQLite (2000, D. Richard Hipp).** Hipp was building software for a US Navy destroyer and wanted a database that didn't need a server to be running or an administrator to configure it — the program should just open a file. SQLite's tagline is *"a replacement for `fopen()`"*, not "a replacement for Oracle." The entire database is a single file, and the engine is a C library that runs inside the host process. There is no separate database process at all.

**PostgreSQL (1986 → POSTGRES at Berkeley, SQL support 1994).** PostgreSQL descends from academic research into extensible, object-relational databases. Its target was always the *shared* database: many users, many concurrent connections, long-lived data that must survive crashes and outlive any single client. That requires a process that is always running, that owns the data files, and that arbitrates between clients.

So the two systems answer different questions:
- SQLite: *"How do I give one application structured, transactional local storage with zero setup?"*
- PostgreSQL: *"How do I let hundreds of clients safely share one consistent dataset?"*

---

## 2. Architecture Overview

### SQLite — embedded / in-process

```
┌─────────────────────────────────────────┐
│           Application process            │
│                                          │
│   app code  ──►  SQLite library (C)      │
│                    │  SQL compiler       │
│                    │  VDBE (bytecode)    │
│                    │  B-tree layer       │
│                    │  Pager + cache      │
│                    ▼                      │
│                  OS file I/O              │
└────────────────────┬─────────────────────┘
                     ▼
              one file:  app.db
              (+ app.db-wal / -journal)
```
No IPC, no network, no server. A function call goes straight down to a `read()`/`write()` on the database file. Concurrency between processes is coordinated entirely through **file locks** on that one file.

### PostgreSQL — client-server, process-per-connection

When a client connects, the **postmaster** forks a dedicated **backend process** for that connection. All backends share one region of shared memory (**shared buffers**, the page cache) and coordinate through it. Background processes (WAL writer, checkpointer, autovacuum, background writer) handle durability and cleanup. One connection = one OS process.

---

## 3. Internal Design

### Storage layout — measured

| Property | SQLite | PostgreSQL |
|---|---|---|
| Default page size | **4 KB** (`PRAGMA page_size`) | **8 KB** (`SHOW block_size`) |
| File organization | **one file** holds all tables + indexes + schema | **one file per relation** (table, index), plus `pg_wal/`, catalogs |
| `students`+`enrollments`+2 indexes | 1,482 pages in a single **6.07 MB** `lab.db` | `enrollments` heap alone = 1,274 × 8 KB ≈ **10 MB**, separate file |
| Schema storage | `sqlite_schema` table (page 1) | system catalogs (`pg_class`, `pg_attribute`, …) |

SQLite's `dbstat` virtual table shows how the *single file* is internally divided:

```
name             bytes     pages
enrollments      3.13 MB    764
idx_enr_student  2.19 MB    534
students         0.54 MB    133
idx_students_dept 0.20 MB    50
```

### Index implementation
Both use **B-trees** for ordered indexes. The notable structural difference is the *primary table*:
- **SQLite**: a table with an `INTEGER PRIMARY KEY` becomes a B-tree *keyed on the rowid* — the table **is** a clustered B-tree (like InnoDB). Other tables are stored as a B-tree on an implicit `rowid`.
- **PostgreSQL**: tables are **heaps** (unordered). Every index, even the primary key, is a *separate* B-tree pointing into the heap by `ctid`. Postgres has no clustered index (only a one-shot `CLUSTER` command that physically reorders once).

### Query execution — measured

The same join, two different strategies:

```
-- SQLite (EXPLAIN QUERY PLAN)
SEARCH s USING COVERING INDEX idx_students_dept (dept=?)
SEARCH e USING COVERING INDEX idx_enr_student (student_id=?)   -- nested loop
```
```
-- PostgreSQL (EXPLAIN ANALYZE)
Parallel Hash Join  (Workers Launched: 1)
  Hash Cond: e.student_id = s.id
  -> Parallel Seq Scan on enrollments
  -> Bitmap Heap Scan on students (dept='CS')
```

SQLite always uses **nested-loop joins** driven by indexes — simple, low-memory, great when the working set is tiny. PostgreSQL has a **cost-based planner** that picked a **hash join** and even spun up a **parallel worker**, because for a 200k-row scan a hash join beats nested loops.

### Transactions, concurrency & durability

| | SQLite | PostgreSQL |
|---|---|---|
| Concurrency unit | **whole database** (file-level locks) | **per-row** (MVCC) |
| Default isolation | Serializable | Read Committed |
| Writers | **one at a time** for the whole DB | many concurrent writers, blocked only on the same row |
| Readers vs writer | WAL mode: readers + 1 writer concurrent | readers never block writers, writers never block readers (MVCC) |
| Durability log | rollback journal (default) or **WAL** (`-wal` file) | **WAL** (`pg_wal/`), `fsync=on`, `synchronous_commit=on` |

---

## 4. Design Trade-Offs

**Why SQLite is embedded.** Zero configuration, zero IPC, the whole DB is one portable file. The cost: the file-level write lock means it is unsuitable for many concurrent writers.

**Why PostgreSQL is client-server.** A always-on server process can arbitrate many writers via row-level MVCC, enforce permissions, run background vacuum/checkpoint, and stream WAL to replicas. The cost: you must run and administer a server, and every query pays IPC + planning + MVCC overhead.

**The overhead shows up even on tiny data.** A trivial `COUNT` took **~0.5 ms** in PostgreSQL but **<0.05 ms** in SQLite. On *large multi-user* workloads the ranking inverts: Postgres's parallelism, MVCC concurrency, and shared cache win decisively.

---

## 5. Key Learnings

1. **One architectural choice explains the rest.** "Library vs server" cascades into file-level vs row-level locking, one-file vs file-per-relation, nested-loop vs cost-based parallel plans, and single-writer vs MVCC.
2. **SQLite wins on the small and local; PostgreSQL wins on the shared and large.**
3. **"Why SQLite for mobile?"** — no server to run, one file to back up/ship, tiny footprint, in-process speed.
4. **"Why PostgreSQL for large multi-user systems?"** — MVCC concurrency, per-relation storage, a cost-based parallel planner, and a hardened durability path.
5. **Both are "correct" designs.** They optimize for different constraints.

---
