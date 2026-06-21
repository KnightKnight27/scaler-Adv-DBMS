# MySQL / InnoDB Storage Engine

## 1. Problem Background

MySQL's original default storage engine, MyISAM, had no crash-safe transaction
support — a power loss could leave tables corrupted, and there was no way to
roll back a multi-statement change. **InnoDB** (originally a third-party engine,
later acquired by Oracle and made MySQL's default in 5.5) was built specifically
to bring ACID transactions, row-level locking, and crash recovery to MySQL while
keeping good performance for OLTP workloads — many small, concurrent
read/write transactions, which is exactly the workload web applications and
e-commerce systems generate. Its architecture reflects an Oracle-database
heritage: clustered storage, undo logs for rollback and MVCC, and redo logs for
durability — a different lineage from Postgres's append-only MVCC, solving the
same correctness problem with a different mechanism.

## 2. Architecture Overview

```
                ┌───────────────────────────────────────────────────────┐
                │                      MySQL Server                     │
                │     SQL Layer (parser, optimizer) — engine-agnostic   │
                └───────────────────────────┬───────────────────────────┘
                                            │ storage engine API
                                            ▼
                ┌────────────────────────────────────────────────────────--┐
                │                       InnoDB Engine                      │
                │  ┌─────────────┐   ┌──────────────┐   ┌────────────────┐ │
                │  │ Buffer Pool │   │  Undo Logs   │   │   Lock Manager │ │
                │  │ (clustered  │   │ (rollback +  │   │(row-level locks│ │
                │  │ index pages)│   │  MVCC reads) │   │  gap locks)    | │
                │  └──────┬──────┘   └──────┬───────┘   └────────────────┘ │
                └─────────┼─────────────────┼─────────────────────────-───┘
                          │ flush           │ written before commit
                          ▼                 ▼
                ┌──────────────────┐  ┌───────────────────────┐
                │  Tablespace files│  │ Redo Log (ib_logfile) │
                │ (.ibd, clustered │  │ circular, fixed size  │
                │ B+tree per table)│  └───────────────────────┘
                └──────────────────┘
```

InnoDB plugs into MySQL through the pluggable storage engine API — the SQL
layer parses and plans queries generically, and InnoDB is responsible only for
how rows are physically stored, locked, and recovered.

## 3. Internal Design

### 3.1 Clustered Index / Primary Key Storage

The single most consequential InnoDB design decision: **every table is stored
as a B+-tree clustered on its primary key.** The leaf pages of this tree
contain the *actual row data*, not just a pointer to it. If a table has no
explicit primary key, InnoDB generates a hidden 6-byte row ID and clusters on
that instead.

This means a primary-key lookup is a single B+-tree descent that lands
directly on the full row — there is no second indirection step the way
Postgres's TID-based heap lookup requires. **Why clustered indexes improve
lookup performance**: range scans on the primary key are sequential disk reads
(rows with adjacent keys are physically adjacent on disk), and primary-key
point lookups never need a second I/O to fetch the row body.

### 3.2 Secondary Indexes

Secondary indexes are separate B+-trees, but their leaf pages store the
**primary key value**, not a physical row pointer. So a secondary-index lookup
is a two-step descent: descend the secondary index to find the matching
primary key, then descend the clustered index using that key to fetch the full
row. This is structurally similar to SQLite's index-then-table-Btree pattern,
and is a direct trade-off: it means secondary indexes never need to be
updated when a row is physically moved on the clustered tree (e.g. during a
page split), because they reference the primary key, not a physical location.

### 3.3 Buffer Pool

Analogous to Postgres's shared buffers — a large in-memory cache of
16KB pages (InnoDB's default page size) holding clustered and secondary index
pages. It uses a modified LRU (with a "young"/"old" sublist split) specifically
to prevent large sequential scans from flushing out the working set of
frequently used pages — a problem plain LRU is vulnerable to and that
Postgres's clock-sweep also has to defend against via similar heuristics
(buffer ring strategy for big sequential scans).

### 3.4 Undo Logs

Undo logs are InnoDB's mechanism for both **transaction rollback** and
**MVCC**. When a row is updated, InnoDB modifies it **in place** but first
writes the row's previous version into an undo log segment, and the new row
version gets a pointer (a roll-pointer) back to that undo record.
- **Rollback**: if the transaction aborts, InnoDB walks the undo chain and
  restores the previous version(s) — this is true physical undo, unlike
  Postgres which simply discards the new tuple version and never had to
  "restore" anything because the old one was never overwritten.
- **MVCC reads (Oracle-style)**: a transaction that needs to see an older
  snapshot of a row follows the roll-pointer chain backward through undo log
  versions until it finds the version visible to its transaction's read view.
  This is the opposite direction from Postgres: Postgres keeps *old* versions
  sitting in the heap until vacuumed; InnoDB keeps the *current* version in
  the table and reconstructs *old* versions on demand from undo logs.

**Purpose of undo logging, summarized**: give a transaction the ability to (a)
reverse its own in-place changes if it aborts, and (b) let other transactions
reconstruct a consistent-as-of-their-snapshot view of a row without blocking
the writer that's currently holding it.

### 3.5 Redo Logs

The redo log is InnoDB's WAL equivalent — a circular, fixed-size on-disk log
(`ib_logfile0`/`ib_logfile1`, or modern redo log files) recording physical page
changes. Exactly as in Postgres, **a redo record must be durably written
before the commit is acknowledged**, and the rule "redo before data page
flush" lets InnoDB use a buffer pool that lazily flushes dirty pages, while
guaranteeing recoverability.

**Purpose of redo logging**: durability and crash recovery — replay redo since
the last checkpoint to restore all committed-but-not-yet-flushed changes; this
is the InnoDB analog of Postgres's WAL replay.

**Why does InnoDB need both undo and redo logs?** They solve different
failure scenarios and operate at different layers:
- Redo answers "how do we survive a crash without losing committed changes
  that were only in memory" (durability of new state).
- Undo answers "how do we cancel a transaction's own changes, or let another
  transaction see an older version of a row that's been overwritten in
  place" (rollback + MVCC).
A system without undo logs but with in-place updates would have no way to
roll back a multi-statement transaction or service an old-snapshot read
without blocking. A system without redo would have no way to recover updates
that were acknowledged as committed but only existed in the buffer pool at
crash time. The two logs are not redundant — they're solving rollback/MVCC and
durability respectively, and InnoDB needs both *because* it updates rows
in-place (Postgres needs neither in the same form, because it never overwrites
a tuple — its append-only design absorbs both jobs into the heap + WAL).

### Undo vs Redo Log Responsibilities

```text
                Row Update
                     |
          -----------------------
          |                     |
          v                     v
     Undo Log              Redo Log
          |                     |
          v                     v
 Transaction Rollback    Crash Recovery
 MVCC Snapshot Reads     Durability
```

### 3.6 Locking — Row-Level Locks and Gap Locks

InnoDB takes locks on individual index records (not whole pages or tables) for
row-level concurrency. Beyond ordinary record locks, InnoDB also uses **gap
locks** — locks on the *space between* index records — to prevent phantom
reads under `REPEATABLE READ` isolation. A range query like
`SELECT ... WHERE id BETWEEN 10 AND 20 FOR UPDATE` locks not just existing rows
in that range but the gaps between them, so another transaction can't insert a
new row into the range and create a phantom that wasn't there at the start of
the transaction. The combination of a record lock plus the gap before it is
called a **next-key lock**, and it's the mechanism by which InnoDB achieves
phantom-read prevention at `REPEATABLE READ` without needing Serializable
Snapshot Isolation-style predicate locking.

### 3.7 Isolation Levels

InnoDB supports all four SQL isolation levels, but its default —
`REPEATABLE READ` — is unusually strong because of next-key locking: it
prevents phantoms in locking reads, something the SQL standard doesn't
strictly require at that level. Postgres's `REPEATABLE READ`, by contrast,
relies on snapshot isolation and does *not* use gap-style locks, achieving a
related-but-distinct guarantee (snapshot consistency rather than locking-based
phantom prevention).

## 4. Key Comparison with PostgreSQL

| Aspect | PostgreSQL | InnoDB |
|---|---|---|
| Update strategy | Append new tuple version | In-place overwrite |
| Old-version handling | Stays in heap, removed later by VACUUM | Captured in undo log before overwrite |
| MVCC direction | Old versions sit forward in the heap | Old versions reconstructed backward from undo |
| Primary storage layout | Heap, unordered | Clustered B+-tree on primary key |
| Garbage collection | VACUUM (separate background process) | Undo log purge (background purge threads) |
| Phantom prevention | Snapshot isolation (no extra locks) | Next-key / gap locks |

## 5. Design Trade-Offs

- **Clustered storage** gives fast primary-key range scans and point lookups,
  but secondary-index lookups cost an extra tree descent, and a **wide or
  randomly-increasing primary key bloats every secondary index**, since the PK
  value is duplicated into every secondary index leaf — this is why InnoDB
  schemas are commonly designed around short, monotonically increasing
  primary keys (e.g. auto-increment integers rather than UUIDs).
- **In-place update + undo logging** avoids the heap bloat / VACUUM overhead
  Postgres pays, but pays instead with undo log growth under long-running
  transactions (a long-open transaction can prevent undo purge, similar in
  spirit to how a long-open Postgres transaction prevents VACUUM from
  reclaiming dead tuples — both systems have an analogous "old transaction
  blocks cleanup" failure mode, just implemented through different logs).
- **Gap locks** give stronger phantom-read protection at `REPEATABLE READ`,
  but increase lock contention and the chance of deadlocks on range
  predicates compared to Postgres's lock-free snapshot approach.

**Why did PostgreSQL choose a different MVCC model?** Postgres's MVCC predates
InnoDB's MySQL integration and was designed to avoid the complexity of
undo-based rollback: by never overwriting data, a crash never needs an undo
phase at all — only redo. The cost it accepts instead is needing an active
vacuum process to bound heap growth. InnoDB instead inherited an
Oracle-style design that prioritizes compact primary storage (no growing heap
of dead tuples) at the cost of needing both undo and redo machinery.

## 6. Experiments / Observations

```sql
-- Demonstrating next-key (gap) locking under REPEATABLE READ
-- Session A:
START TRANSACTION;
SELECT * FROM orders WHERE id BETWEEN 100 AND 200 FOR UPDATE;

-- Session B (concurrently):
START TRANSACTION;
INSERT INTO orders (id, customer_id) VALUES (150, 7);
-- Session B blocks until Session A commits/rolls back,
-- even though id=150 did not previously exist —
-- this is the gap lock between existing keys 100..200 in action.
```

Inspecting `SHOW ENGINE INNODB STATUS` after such a scenario reveals the
`TRANSACTIONS` section listing the held lock as a `RECORD LOCKS ... gap before
rec`, directly surfacing the next-key lock mechanism described above.

## 7. Key Learnings

- "Where is the old version of a row?" is the single most useful question for
  distinguishing MVCC implementations: Postgres keeps it in the heap until
  vacuumed; InnoDB reconstructs it from undo logs after an in-place overwrite.
- Undo and redo logs are not two versions of the same idea — they answer
  different questions (rollback/MVCC vs. crash durability) and InnoDB needs
  both specifically because it mutates data in place.
- Clustering primary storage on the primary key is a powerful read
  optimization that has a real write-side cost on every secondary index,
  which directly shapes good primary-key design (small, sequential keys).
- Locking-based phantom prevention (gap locks) and snapshot-based phantom
  prevention (Postgres's snapshot isolation) are two legitimate, different
  answers to the same isolation-level requirement, each with different
  contention/throughput trade-offs.

## Architectural Lessons

InnoDB demonstrates a fundamentally different approach to solving the same problems addressed by PostgreSQL. Rather than preserving old row versions inside the table itself, InnoDB performs in-place updates and relies on undo logs to reconstruct historical versions when needed.

The combination of clustered indexes, undo logs, redo logs, and row-level locking allows InnoDB to provide strong transactional guarantees while maintaining good OLTP performance.

The most important lesson is that database systems can achieve ACID guarantees through very different internal architectures. PostgreSQL and InnoDB solve concurrency, durability, and recovery using different mechanisms, but both arrive at the same correctness guarantees through carefully engineered trade-offs.

## References
- InnoDB source: `storage/innobase/` in the MySQL server source tree
- MySQL Reference Manual: InnoDB Locking, Transaction Model, Undo/Redo Logs
- "MySQL Internals Manual" — bundled with MySQL source distribution

