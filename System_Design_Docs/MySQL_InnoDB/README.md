# MySQL / InnoDB Storage Engine

> Advanced DBMS — System Design Discussion
> Topic 3: MySQL / InnoDB Storage Engine

---

## Table of Contents

1. [Problem Background](#1-problem-background)
2. [Architecture Overview](#2-architecture-overview)
3. [Internal Design](#3-internal-design)
4. [Design Trade-Offs](#4-design-trade-offs)
5. [Experiments / Observations](#5-experiments--observations)
6. [Key Learnings](#6-key-learnings)
7. [References](#references)

---

## 1. Problem Background

MySQL is a client-server relational database, but its real workhorse is its **storage
engine**, and since MySQL 5.5 the default is **InnoDB**. MySQL has a pluggable
storage-engine architecture: the server handles connections, parsing, and planning,
while a storage engine handles the actual on-disk storage, indexing, locking, and
transactions. InnoDB was created by **Innobase Oy (Heikki Tuuri), first released in
2001**, to give MySQL what the older MyISAM engine lacked: **ACID transactions,
crash recovery, row-level locking, and foreign keys**.

The problem InnoDB solves: *provide a transactional, crash-safe, highly concurrent
storage engine for OLTP workloads* — many short read/write transactions hitting the
same tables. Its design borrows heavily from Oracle (undo-based MVCC, redo logging,
clustered storage), and the interesting story is how its choices differ from
PostgreSQL's while solving the same ACID + concurrency problem.

---

## 2. Architecture Overview

```
        Clients ─► MySQL Server layer
                     (connection mgmt, parser, optimizer, SQL layer)
                                │  handler API
                     ┌──────────▼───────────── InnoDB storage engine ─────────────┐
                     │                                                             │
                     │   IN MEMORY:                                                │
                     │   ┌──────────────────────────────────────────────────┐     │
                     │   │  Buffer Pool  (data + index pages, LRU)            │     │
                     │   │   • Change buffer  • Adaptive hash index           │     │
                     │   └──────────────────────────────────────────────────┘     │
                     │   Log buffer                                                │
                     │                                                             │
                     │   ON DISK:                                                  │
                     │   ┌─────────────┐  ┌───────────┐  ┌──────────────────┐      │
                     │   │ Tablespaces │  │ Redo log   │  │ Undo logs        │     │
                     │   │ (.ibd):     │  │ (ib_logfile│  │ (rollback +      │     │
                     │   │ clustered   │  │  / redo)   │  │  MVCC versions)  │     │
                     │   │ B+tree data │  │ WAL-style  │  │                  │     │
                     │   └─────────────┘  └───────────┘  └──────────────────┘      │
                     │   Doublewrite buffer (torn-page protection)                 │
                     └─────────────────────────────────────────────────────────────┘
```

**Two-layer architecture.** The **server layer** is engine-agnostic (it parses SQL,
optimizes, and manages connections). InnoDB plugs in underneath via the handler API.
This is why MySQL can offer multiple engines — but in practice InnoDB is the answer for
transactional work.

**Write data flow (simplified):** modify a page in the **buffer pool** (mark dirty) →
write a **redo** record to the log buffer → flush redo on commit (durability) → write
the **before-image** to the **undo log** (for rollback + MVCC) → dirty pages flushed
to the tablespace lazily, protected by the **doublewrite buffer**.

---

## 3. Internal Design

### 3.1 Clustered Index & Primary-Key Storage

The defining InnoDB decision: **the table *is* its primary-key B+tree.** This is a
**clustered index** — the leaf nodes of the primary-key B+tree contain the **full
rows**, stored in primary-key order. There is no separate unordered heap (contrast
PostgreSQL).

- If you declare a `PRIMARY KEY`, the table is clustered on it.
- If you don't, InnoDB uses the first non-null `UNIQUE` index; failing that, it creates
  a hidden 6-byte `DB_ROW_ID`.
- Consequence: **primary-key lookups are extremely fast** — one B+tree descent lands
  directly on the row data, no extra indirection.
- Consequence: rows physically ordered by PK → range scans on the PK are sequential
  and cache-friendly. This is why **monotonic PKs (e.g. AUTO_INCREMENT)** are preferred:
  random PKs (like random UUIDs) cause page splits and fragmentation on insert.

### 3.2 Secondary Indexes

A secondary index is a separate B+tree keyed by the indexed column(s). Its **leaf
entries store the indexed value + the primary key** of the row (not a physical pointer).

- A lookup via a secondary index therefore does **two** searches: traverse the
  secondary index to get the PK, then traverse the **clustered index** to fetch the row.
  This is a **bookmark lookup**.
- **Covering index:** if the secondary index already contains every column the query
  needs, InnoDB skips the second lookup entirely.
- Because secondary indexes reference rows by **PK value** (a logical pointer, not a
  physical address), rows can move on disk (page splits) without rewriting secondary
  indexes — but it makes the PK part of every secondary index, so a wide PK bloats all
  indexes.

```
Clustered (primary) B+tree          Secondary index B+tree
   leaf = full row, ordered by PK       leaf = (indexed_col, PK)
   ┌──────────────────────────┐         ┌────────────────────────┐
   │ PK=1 | full row data ...  │ ◄──────┤ 'Ann'  → PK=42          │
   │ PK=2 | full row data ...  │  PK     │ 'Bob'  → PK=7           │
   │ ...                       │  lookup │ 'Cara' → PK=1           │
   └──────────────────────────┘         └────────────────────────┘
```

### 3.3 Buffer Pool

The **buffer pool** is InnoDB's in-memory cache of data and index pages (default 16 KB
pages) — the analogue of PostgreSQL's shared buffers, and usually the single most
important tuning knob (`innodb_buffer_pool_size`).

- **Replacement:** a **variant of LRU split into "young" and "old" sublists.** Newly
  read pages enter the *old* (midpoint) sublist; only if accessed again do they migrate
  to the *young* end. This **midpoint insertion** prevents a single large scan from
  flushing the hot working set out of cache.
- **Change buffer:** for *secondary-index* pages not currently in the pool, changes
  from inserts/updates/deletes are buffered and merged later, avoiding random read I/O
  on every write to a non-resident index page.
- **Adaptive hash index:** InnoDB watches access patterns and builds an in-memory hash
  index over hot pages for O(1) point lookups.

### 3.4 MVCC via Undo Logs (Oracle-style)

InnoDB provides MVCC, but **completely differently from PostgreSQL.** It updates rows
**in place** and keeps **old versions in the undo log**, not in the table itself.

- Each clustered-index row carries hidden columns: **`DB_TRX_ID`** (the last
  transaction to modify it) and **`DB_ROLL_PTR`** (a pointer into the undo log to the
  *previous* version of the row).
- On an `UPDATE`, InnoDB writes the new values into the row in place and pushes the
  **old version into the undo log**, linked via `DB_ROLL_PTR`. Old versions form a
  chain.
- A transaction reads through a **read view** (snapshot). If the current row's
  `DB_TRX_ID` is too new to be visible, InnoDB walks the `DB_ROLL_PTR` chain into the
  undo log to **reconstruct the version** the transaction is allowed to see.
- When no active transaction can still need an old version, a background **purge**
  thread discards the corresponding undo records (and removes delete-marked rows).

> **Contrast with PostgreSQL:** PostgreSQL keeps all versions *inline in the heap* and
> cleans them with `VACUUM`. InnoDB keeps the *current* version in place and old
> versions *separately* in undo logs, cleaned by *purge*. Different place, same idea.

### 3.5 Undo Logs and Redo Logs — Why Both?

InnoDB needs **two** logs because they answer two different questions:

- **Undo log — "how do I go backward?"** Stores **before-images** of changed rows.
  Used for (a) **transaction rollback** (`ROLLBACK` or a failed statement) and (b)
  **MVCC** (reconstructing old versions for snapshots). It provides **atomicity** and
  consistent reads.
- **Redo log — "how do I go forward after a crash?"** A **write-ahead log** of physical
  page changes (`ib_logfile` / redo log files). On crash recovery, InnoDB **replays
  redo** to re-apply committed changes that hadn't yet been flushed from the buffer
  pool to the tablespace. It provides **durability**.

Recovery sequence after a crash: **redo** forward to reach the latest persisted state,
then **undo** the changes of transactions that were not committed. (Undo records are
themselves protected by redo, so they survive a crash too.)

### 3.6 Locking & Concurrency

InnoDB does **row-level locking**, which is central to its OLTP concurrency.

- **Shared (S) / Exclusive (X) row locks** on individual index records.
- **Record locks** lock an index record; **gap locks** lock the *gap between* index
  records (no row there) to stop phantom inserts; a **next-key lock** = record lock +
  the gap before it. Gap/next-key locks are how **REPEATABLE READ** prevents phantoms.
- **Intention locks** (IS/IX) at the table level signal "some rows are locked below,"
  so a table-level lock request can be checked cheaply.
- Because locks are on **index records**, a query that can't use an index may lock far
  more rows than expected — indexing affects *locking*, not just speed.
- Deadlocks are detected automatically and one transaction is rolled back.

### 3.7 Isolation Levels

InnoDB supports all four SQL isolation levels; the default is **REPEATABLE READ**.

| Level | Dirty read | Non-repeatable read | Phantom |
|---|---|---|---|
| READ UNCOMMITTED | possible | possible | possible |
| READ COMMITTED | no | possible | possible |
| **REPEATABLE READ** (default) | no | no | **prevented** (gap/next-key locks) |
| SERIALIZABLE | no | no | no (reads take shared locks) |

Under REPEATABLE READ, a transaction establishes its **read view** at the first read
and sees a consistent snapshot for its lifetime; gap locking additionally blocks
phantom inserts in ranges it has touched.

### 3.8 Durability Extras: Doublewrite & flush settings

- **Doublewrite buffer:** before writing a data page to its final location, InnoDB
  writes it to a contiguous doublewrite area first. If a crash causes a **torn page**
  (half-written 16 KB page), recovery restores the intact copy from the doublewrite
  buffer. (PostgreSQL solves the same torn-page problem with full-page writes in WAL.)
- **`innodb_flush_log_at_trx_commit`** controls the durability/throughput trade: `1`
  (default) flushes redo on every commit (fully ACID); `2`/`0` relax this for speed at
  the risk of losing the last ~1s of transactions on a crash.

---

## 4. Design Trade-Offs

### Clustered index
- **+** PK lookups and PK-ordered range scans are very fast (data is *in* the index).
- **+** No separate heap; good locality for the common "fetch by primary key" path.
- **−** Secondary lookups cost an extra clustered-index traversal (PK is the bookmark).
- **−** The PK is duplicated into every secondary index → a wide PK bloats all indexes.
- **−** Random/UUID primary keys cause page splits & fragmentation on insert; monotonic
  keys are strongly preferred.

### Undo-based MVCC (vs PostgreSQL's inline versions)
- **+** The table stays compact — only the *current* version lives in the data pages,
  so it does **not** bloat the way an update-heavy PostgreSQL heap can.
- **+** No table-wide `VACUUM`; cleanup is the lighter-weight **purge** of undo.
- **−** Reading an old version requires **walking the undo chain** — long-running
  transactions force long version chains, slowing reads and **delaying purge**
  (the InnoDB analogue of "vacuum can't keep up").
- **−** Rollback can be expensive (must apply undo).

### Two logs (undo + redo)
- **+** Clean separation: atomicity/MVCC (undo) vs durability (redo); each optimized
  for its job; redo writes are sequential and fast.
- **−** More moving parts and more write paths than a single-log design; redo causes
  **write amplification** (changes written to redo and later to pages).

### Row-level + gap locking
- **+** High write concurrency; REPEATABLE READ without losing phantom protection.
- **−** Gap/next-key locks can block inserts surprisingly broadly and are a common
  source of deadlocks; locking quality depends on having the right indexes.

### vs PostgreSQL (summary)
| | InnoDB | PostgreSQL |
|---|---|---|
| Row storage | Clustered B+tree (table = PK index) | Unordered heap + separate indexes |
| Updates | **In place** | New tuple version (append-mostly) |
| Old versions kept in | **Undo log** (separate) | **Heap, inline** |
| Version cleanup | **Purge** thread | **VACUUM** / autovacuum |
| Secondary index → row | via **PK** (logical) | via **CTID** (physical) |
| Default isolation | REPEATABLE READ | READ COMMITTED |
| Torn-page protection | Doublewrite buffer | Full-page writes in WAL |

---

## 5. Experiments / Observations

### 5.1 Clustered vs secondary-index lookup

```sql
-- Primary-key lookup: single clustered-index traversal.
EXPLAIN FORMAT=TREE
SELECT * FROM students WHERE id = 42;          -- access type: const / eq_ref on PRIMARY

-- Secondary-index lookup: index search THEN clustered-index fetch (bookmark lookup).
CREATE INDEX idx_name ON students(name);
EXPLAIN FORMAT=TREE
SELECT * FROM students WHERE name = 'Abdur';   -- ref on idx_name, then PK lookup

-- Covering index: no second lookup needed (note "Using index" in classic EXPLAIN).
EXPLAIN
SELECT id, name FROM students WHERE name = 'Abdur';  -- Extra: Using index
```
**Observation:** `SELECT *` by `name` must look up the clustered index after the
secondary index, whereas selecting only covered columns (`id, name`) reports
`Using index` — the second traversal is skipped. This is §3.2 directly.

### 5.2 Watching MVCC / undo with two sessions

```sql
-- Session A
START TRANSACTION;
SELECT balance FROM accounts WHERE id = 1;     -- e.g. 100; read view established

-- Session B
UPDATE accounts SET balance = 500 WHERE id = 1;
COMMIT;                                         -- in-place update; old value -> undo log

-- Session A (same transaction, REPEATABLE READ)
SELECT balance FROM accounts WHERE id = 1;     -- STILL 100 (reconstructed from undo)
COMMIT;
SELECT balance FROM accounts WHERE id = 1;     -- now 500
```
**Observation:** Session A keeps seeing the old value mid-transaction because InnoDB
rebuilds it by following `DB_ROLL_PTR` into the **undo log** — even though the row was
already updated in place. This makes undo-based MVCC concrete.

### 5.3 Row-level locking and gap locks

```sql
-- Session A
START TRANSACTION;
SELECT * FROM accounts WHERE id BETWEEN 10 AND 20 FOR UPDATE;  -- next-key locks the range

-- Session B
INSERT INTO accounts(id, balance) VALUES (15, 0);  -- BLOCKS: gap lock prevents phantom
UPDATE accounts SET balance=1 WHERE id = 100;       -- succeeds: outside the locked range
```
**Observation:** the insert into the locked gap blocks (phantom prevention under
REPEATABLE READ) while a write outside the range proceeds — demonstrating row/gap
locking, not table locking.

### 5.4 Inspecting engine internals

```sql
SHOW ENGINE INNODB STATUS\G          -- transactions, locks, log/checkpoint LSNs, deadlocks
SELECT * FROM performance_schema.data_locks;          -- current row/gap locks
SHOW VARIABLES LIKE 'innodb_buffer_pool_size';
SHOW VARIABLES LIKE 'innodb_flush_log_at_trx_commit'; -- durability vs speed knob
SHOW ENGINE INNODB STATUS\G  -- see "History list length" = pending undo (purge backlog)
```
**Observation:** a rising **History list length** indicates old versions are piling up
in undo (often due to a long-running transaction blocking purge) — the InnoDB
counterpart to PostgreSQL bloat from delayed vacuum.

---

## 6. Key Learnings

1. **Clustering is InnoDB's organizing idea.** Because the table *is* the PK B+tree,
   primary-key access is a single descent, secondary indexes reference rows by PK, and
   PK choice (monotonic vs random, narrow vs wide) silently governs insert performance
   and index size.

2. **Two logs, two jobs.** Undo answers "go backward" (rollback + MVCC snapshots);
   redo answers "go forward after a crash" (durability). Recovery is redo-then-undo.
   You cannot drop either: one gives atomicity/isolation, the other durability.

3. **Same MVCC goal, opposite mechanism vs PostgreSQL.** InnoDB updates in place and
   stores old versions in undo (cleaned by purge); PostgreSQL writes new versions
   inline and cleans with VACUUM. The trade is *table-bloat + VACUUM* (PG) vs
   *undo-chain walks + purge lag* (InnoDB) — neither escapes the cost of keeping old
   versions, they just put it in different places.

4. **Locks live on index records.** Row-level, gap, and next-key locks give high
   concurrency and phantom protection, but their reach depends on indexing — a missing
   index can turn a row lock into a near-table lock and breed deadlocks.

5. **Durability is layered.** Redo (WAL) plus the **doublewrite buffer** (torn-page
   protection) plus `innodb_flush_log_at_trx_commit` together define how much you can
   lose on a crash — an explicit, tunable durability/throughput trade-off.

---

## References

- MySQL Reference Manual — InnoDB Storage Engine: https://dev.mysql.com/doc/refman/8.0/en/innodb-storage-engine.html
- InnoDB Architecture diagram & overview: https://dev.mysql.com/doc/refman/8.0/en/innodb-architecture.html
- InnoDB Multi-Versioning (undo, DB_TRX_ID, DB_ROLL_PTR): https://dev.mysql.com/doc/refman/8.0/en/innodb-multi-versioning.html
- InnoDB Locking (record, gap, next-key, intention): https://dev.mysql.com/doc/refman/8.0/en/innodb-locking.html
- InnoDB Buffer Pool & midpoint LRU: https://dev.mysql.com/doc/refman/8.0/en/innodb-buffer-pool.html
- Redo log, undo log, doublewrite buffer, checkpoints — InnoDB On-Disk Structures docs
- Jeremy Cole — "InnoDB internals" blog series (B+tree & record layout)

---

*Submitted for the Advanced DBMS System Design Discussion. All analysis and prose are
original; cited sources were used for fact-checking architectural details.*
