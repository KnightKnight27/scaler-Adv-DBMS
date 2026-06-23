# MySQL / InnoDB Storage Engine

**Clustered Indexes · Buffer Pool · Undo & Redo Logs · Row-Level Locking · Gap Locks**

> System Design Discussion — Advanced DBMS
> Author: Lavya Tanotra (24BCS10124)
> Topic: MySQL / InnoDB Storage Engine

---

## 1. Problem Background

MySQL is one of the most widely deployed databases in the world, and almost all of that deployment runs on a single storage engine: **InnoDB** (the default since MySQL 5.5). A *storage engine* is the component that actually decides how rows are laid out on disk, how indexes are organised, how transactions are made durable, and how concurrent writers are kept from corrupting each other.

InnoDB exists to give MySQL the properties that its original default engine (MyISAM) lacked:

- **ACID transactions** — commit/rollback with durability, not just "best-effort" writes.
- **Crash recovery** — survive a power loss without a manual repair step.
- **Row-level concurrency** — many transactions writing different rows at the same time, instead of locking an entire table.
- **Referential integrity** — foreign keys actually enforced.

The interesting engineering question is *how* InnoDB delivers all four at once on spinning disks and SSDs that are thousands of times slower than RAM. The answer is a specific set of design choices — a **clustered-index storage layout**, a **buffer pool** that caches pages, **redo logs** for durability, **undo logs** for rollback and consistent reads, and **fine-grained locking** (including gap locks) for isolation. This document explains each choice, why it was made, and what trade-off it carries.

Where useful I contrast InnoDB with PostgreSQL, because the two engines made *opposite* decisions on a few key points (notably how they handle row versions), which makes the trade-offs much clearer.

---

## 2. Architecture Overview

InnoDB is split into an **in-memory** part (caches and buffers) and an **on-disk** part (table data and logs). A set of background threads move data between them.

```
                        ┌──────────────────────────────────────────────┐
   SQL  ───────────────▶│  MySQL Server layer                          │
                        │  (parser → optimizer → executor)             │
                        └───────────────────────┬──────────────────────┘
                                                 │ handler API
                        ┌────────────────────────▼──────────────────────┐
                        │              InnoDB Storage Engine             │
                        │                                                │
                        │   IN-MEMORY                                    │
                        │   ┌──────────────────────────────────────┐    │
                        │   │  Buffer Pool (data + index pages)      │    │
                        │   │  • LRU list  • change buffer           │    │
                        │   │  • adaptive hash index                 │    │
                        │   └───────────────┬──────────────────────┘    │
                        │   ┌───────────────┴──────────┐                 │
                        │   │  Log Buffer (redo records) │                 │
                        │   └───────────────┬──────────┘                 │
                        └───────────────────┼────────────────────────────┘
                                            │
              background threads:  page cleaner · purge · master
                                            │
                ┌───────────────────────────┼───────────────────────────┐
                ▼                           ▼                            ▼
      ┌──────────────────┐       ┌────────────────────┐      ┌────────────────────┐
      │  .ibd tablespace │       │   redo log         │      │   undo logs        │
      │  (clustered B+   │       │   (ib_logfile /     │      │  (rollback segments│
      │   tree pages)    │       │    #innodb_redo)    │      │   in system/undo TS)│
      └──────────────────┘       └────────────────────┘      └────────────────────┘
        durability of DATA pages   durability of CHANGES        rollback + MVCC reads
```

**Write path of a committed UPDATE** (the heart of the design):

```
UPDATE accounts SET bal = 0 WHERE id = 1;

1. find the page in the Buffer Pool (read from disk if missing)
2. write the OLD version of the row into an UNDO log    ─┐ (for rollback + consistent reads)
3. modify the row in the in-memory page (mark page dirty)│
4. append a REDO record describing the change to log buffer
5. COMMIT → flush redo log to disk (fsync)  ◀── durability point
6. (later) page cleaner flushes the dirty data page to the .ibd file
7. (later) purge thread discards undo no longer needed by any reader
```

The key idea: **commit only needs the small, sequential redo log to hit disk.** The actual data pages are written lazily in the background. This is the same "log first, data later" principle every serious database relies on.

---

## 3. Internal Design

### 3.1 Clustered Index — the table *is* a B+Tree

This is InnoDB's most defining choice. In InnoDB **the table data is stored inside the primary-key B+Tree itself**. There is no separate "heap" of rows plus indexes pointing at it (which is how PostgreSQL and MyISAM work). The leaf nodes of the primary-key B+Tree *contain the full rows*.

```
              Clustered index (PRIMARY KEY = id)
                       ┌──────────────┐
            internal   │  [ 50 | 100 ]│
                       └──┬───────┬───┘
              ┌───────────┘       └───────────┐
              ▼                               ▼
   ┌────────────────────────┐    ┌────────────────────────┐
   │ leaf: id=10 → FULL ROW  │    │ leaf: id=100 → FULL ROW │   ← rows live IN the leaf
   │       id=20 → FULL ROW  │    │       id=130 → FULL ROW │
   └────────────────────────┘    └────────────────────────┘
```

Consequences (each is a real design trade-off):

- **Primary-key lookups are very fast** — one B+Tree descent lands directly on the full row; there's no second hop to a heap.
- **Rows are physically ordered by primary key.** Range scans on the PK (`WHERE id BETWEEN …`) are sequential and cache-friendly.
- **Secondary indexes are different.** A secondary index leaf does **not** store a row pointer (TID); it stores the **primary-key value**. So a lookup via a secondary index is a *two-step* process:

```
SELECT * FROM accounts WHERE email = 'x@y.com';

  secondary index (email) ──▶ finds PK value (id = 42)   [step 1]
  clustered index (id)    ──▶ finds the full row          [step 2: "bookmark lookup"]
```

  This is why a **large primary key is expensive in InnoDB**: its bytes are copied into *every* secondary index. Best practice — a small, monotonically increasing PK (e.g. `BIGINT AUTO_INCREMENT`) — falls directly out of this design.

### 3.2 Buffer Pool

The buffer pool is InnoDB's in-memory cache of 16 KB pages; it is usually the single most important tuning knob (`innodb_buffer_pool_size`). All reads and writes go through it.

- **Replacement** uses a **modified LRU** split into a *young* (hot) sublist and an *old* sublist. A newly read page enters at the head of the **old** sublist, not the young one. Only if it is accessed *again* after a short delay is it promoted to the young list. This protects the hot working set from being flushed out by a one-off large scan (e.g. a nightly report touching every page) — a classic "scan resistance" design.
- The **change buffer** lets writes to *secondary index* pages that aren't currently in memory be buffered and merged later, avoiding a random read just to update an index.
- The **adaptive hash index** automatically builds an in-memory hash over hot B+Tree pages so frequent lookups skip the tree descent entirely.

> While building a buffer pool in my team's capstone engine, the lesson was the same one InnoDB encodes: naive LRU is fooled by a single big scan. InnoDB's young/old split is a clean fix that I now understand the motivation for.

### 3.3 Redo Log (durability)

The redo log is a **fixed-size, circular, append-only** log of *physical* changes ("page P, offset O, new bytes B"). It implements **Write-Ahead Logging**: the redo record for a change is flushed before the data page is considered safe to write.

- At **commit**, InnoDB flushes the redo log up to that transaction's log sequence number (**LSN**) and `fsync`s it. That single sequential flush is what makes the commit durable — the random data-page writes can wait.
- On **crash recovery**, InnoDB replays the redo log forward from the last checkpoint, re-applying any change that didn't reach the data files. Recovery is idempotent because each page records the LSN it was last updated to.
- `innodb_flush_log_at_trx_commit` exposes the core durability/performance trade-off directly: `=1` (fsync every commit, fully ACID) vs `=2`/`=0` (flush less often, faster but can lose the last ~1 second on a crash).

### 3.4 Undo Log + MVCC (consistent reads & rollback)

Before a row is modified, InnoDB copies its **previous version** into an **undo log**. That single mechanism serves two purposes:

1. **Rollback** — if the transaction aborts, InnoDB applies the undo records to restore the old values.
2. **MVCC / consistent reads** — a `SELECT` under `REPEATABLE READ` builds a **read view** (snapshot) and, if the current row is newer than its view, **walks the undo chain backwards** to reconstruct the version it *should* see. Each row carries hidden columns `DB_TRX_ID` (last transaction that changed it) and `DB_ROLL_PTR` (pointer to the undo record of the previous version).

```
current row (clustered index):  id=1, bal=0   | DB_TRX_ID=105, DB_ROLL_PTR ─┐
                                                                              ▼
undo log:                        id=1, bal=100 (the version before txn 105)
```

So a reader whose snapshot predates txn 105 follows `DB_ROLL_PTR` and sees `bal=100` — **readers don't block writers**, exactly like PostgreSQL, but achieved differently.

**The key contrast with PostgreSQL:**

| | InnoDB | PostgreSQL |
|---|---|---|
| Where old versions live | separate **undo log** | **in the table itself** (dead tuples) |
| Reading an old version | walk the undo chain to rebuild it | just read the older heap tuple |
| Cleanup | **purge** thread discards old undo | **VACUUM** removes dead tuples |
| Effect on the table | table stays compact | table bloats until vacuumed |
| Cost moved to | reconstructing versions at read time; long transactions bloat undo (history list) | vacuuming + table bloat |

This is the single most illuminating trade-off in the whole topic: **both engines give "readers don't block writers," but InnoDB keeps the table clean and pays at read-time + purge, while PostgreSQL keeps versions inline and pays via VACUUM.**

### 3.5 Locking — row locks and gap locks

InnoDB locks **index records**, not whole tables, which is what enables high write concurrency. Under the default `REPEATABLE READ` it also takes **gap locks** and **next-key locks** to prevent *phantom reads*.

- A **record lock** locks a single index row.
- A **gap lock** locks the *space between* two index values, so no other transaction can `INSERT` into that gap.
- A **next-key lock** = record lock + the gap before it.

```
index values:   … 10 ──gap── 20 ──gap── 30 …

SELECT … WHERE id BETWEEN 15 AND 25 FOR UPDATE;
  → next-key locks cover 20 AND the gaps (10,20) and (20,30)
  → another txn cannot INSERT id=18 or id=22  → no phantom rows appear
```

This is how InnoDB makes `REPEATABLE READ` actually repeatable for range queries. The trade-off: gap locks can block inserts that *intuitively* seem unrelated, and they are a common source of deadlocks — which InnoDB detects automatically and resolves by rolling back the "cheaper" victim transaction.

---

## 4. Design Trade-Offs

| Decision | What InnoDB gains | What it costs |
|---|---|---|
| **Clustered index (rows in the PK B+Tree)** | Very fast PK lookups & PK range scans; no separate heap | Secondary lookups need a second (bookmark) lookup; large PKs bloat every secondary index |
| **Undo-based MVCC** | Tables stay compact; readers don't block writers | Old versions reconstructed at read time; long-running transactions grow the undo history and slow reads |
| **Redo log WAL** | Fast, sequential, durable commits; cheap crash recovery | Changes written twice (log + page); fixed redo size must be tuned |
| **Modified young/old LRU + change buffer** | Scan-resistant cache; fewer random index writes | More complex than plain LRU; change buffer adds recovery/merge work |
| **Row + gap locking under REPEATABLE READ** | High write concurrency; phantom-free range reads | Gap locks block "surprising" inserts; more deadlocks to manage |
| **`flush_log_at_trx_commit=1` default** | Full ACID durability | An `fsync` per commit caps single-thread write throughput |

The unifying philosophy: **InnoDB optimises for transactional OLTP with a clean, primary-key-ordered table, and pushes the costs onto background threads (page cleaner, purge) and onto careful schema design (small PKs).**

---

## 5. Experiments & Observations

### 5.1 Clustered vs secondary index — `EXPLAIN`

```sql
CREATE TABLE accounts (
  id     BIGINT AUTO_INCREMENT PRIMARY KEY,   -- clustered
  email  VARCHAR(120),
  bal    INT,
  KEY idx_email (email)                        -- secondary
) ENGINE=InnoDB;
-- load ~1,000,000 rows

EXPLAIN SELECT * FROM accounts WHERE id = 500000;       -- (A) PK lookup
EXPLAIN SELECT * FROM accounts WHERE email = 'x@y.com'; -- (B) secondary lookup
EXPLAIN SELECT bal FROM accounts WHERE id BETWEEN 1 AND 100; -- (C) PK range
```

Observations:

- **(A)** `type: const / eq_ref`, `key: PRIMARY`, `rows: 1` — single B+Tree descent straight to the full row.
- **(B)** `type: ref`, `key: idx_email` — finds the **PK** in the secondary index, then does the hidden second lookup into the clustered index to fetch `*`. If you `SELECT id` instead of `*`, InnoDB can answer from the secondary index alone (a **covering index**) and skips step 2 — visible as `Extra: Using index`.
- **(C)** `type: range`, `key: PRIMARY` — because rows are physically ordered by `id`, the 100 rows are contiguous pages: a cheap sequential read. The same range on a non-clustered column would scatter across the file.

**Takeaway:** the plan output is direct evidence of §3.1 — InnoDB's clustered layout makes PK access and PK ranges cheap, and makes covering secondary indexes a powerful optimisation.

### 5.2 Watching MVCC / undo via a long transaction

```sql
-- session A
START TRANSACTION;
SELECT * FROM accounts WHERE id = 1;      -- opens a read view; pins undo history

-- session B (meanwhile)
UPDATE accounts SET bal = bal + 1 WHERE id = 1;  -- repeat many times, COMMIT each

-- session A again
SELECT * FROM accounts WHERE id = 1;      -- STILL sees the original value
SELECT count FROM information_schema.innodb_metrics
  WHERE name = 'trx_rseg_history_len';    -- history list length GROWS
```

Observation: session A keeps seeing the old value (consistent read via undo), and the **history list length** climbs because purge can't discard undo that A might still need. This is the concrete cost of the "keep table clean, pay at read time" trade-off — and exactly why long-running transactions are discouraged in MySQL.

### 5.3 Gap locks causing a deadlock

```sql
-- both sessions in REPEATABLE READ
-- session A:   SELECT * FROM accounts WHERE id BETWEEN 10 AND 20 FOR UPDATE;
-- session B:   SELECT * FROM accounts WHERE id BETWEEN 15 AND 25 FOR UPDATE;
-- then each tries to INSERT into the other's gap → InnoDB detects a deadlock
SHOW ENGINE INNODB STATUS\\G   -- LATEST DETECTED DEADLOCK section shows the cycle + victim
```

Observation: `SHOW ENGINE INNODB STATUS` prints the exact lock cycle and which transaction was rolled back. This makes §3.5 tangible — gap locks prevent phantoms but are a real deadlock source, and InnoDB's detector breaks the cycle automatically.

---

## 6. Key Learnings

1. **In InnoDB, the table *is* an index.** The clustered primary-key B+Tree decides everything downstream — fast PK access, secondary-index bookmark lookups, and the rule "keep your primary key small."
2. **Durability and rollback are two different logs.** Redo (physical, forward) makes commits durable and powers crash recovery; undo (logical, backward) powers rollback *and* MVCC consistent reads. Separating them is what lets InnoDB keep the table compact.
3. **MVCC can be done two opposite ways.** InnoDB stores old versions in undo and reconstructs them on read (compact table, purge thread); PostgreSQL stores them inline (simple reads, VACUUM + bloat). Same guarantee, mirror-image costs.
4. **Isolation has a physical price called the gap lock.** Phantom-free `REPEATABLE READ` requires locking the gaps between keys, which trades some insert concurrency and extra deadlocks for correctness.
5. **Most "MySQL performance" advice is this architecture in disguise** — size the buffer pool, use small auto-increment PKs, prefer covering indexes, keep transactions short. Each is a direct consequence of a design choice above.

The broadest lesson: a storage engine is a bundle of trade-offs aimed at a workload. InnoDB targets high-concurrency transactional workloads with a clean, PK-ordered table, and consistently moves cost into background threads and schema discipline. Comparing it to PostgreSQL shows there is rarely one "right" answer in database design — only different costs accepted for different goals.

---

### References

- MySQL 8.0 Reference Manual — *InnoDB Storage Engine* (Clustered & Secondary Indexes, Buffer Pool, Redo/Undo Logs, Locking, Consistent Reads).
- Jeremy Cole, "InnoDB internals" blog series (page/record format, B+Tree layout).
- *High Performance MySQL* (Schwartz, Zaitsev, Tkachenko) — indexing strategy, locking, MVCC.
- `SHOW ENGINE INNODB STATUS`, `INFORMATION_SCHEMA.INNODB_METRICS`, `EXPLAIN` — used for the observations above.
- Cross-reference: PostgreSQL MVCC/VACUUM model, for the InnoDB-vs-PostgreSQL trade-off comparison.
