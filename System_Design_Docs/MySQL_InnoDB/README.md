# MySQL / InnoDB Storage Engine

**Name:** Om Malviya &nbsp;|&nbsp; **Roll Number:** 24BCS10448

> A study of InnoDB: MySQL's default storage engine: focusing on **clustered indexes**, **undo + redo logging**, and **row/gap locking**, and contrasting its **Oracle-style (undo-based) MVCC** with PostgreSQL's append-only model. Every behavior below is demonstrated on a live MySQL **9.6.0** server; commands and real output are quoted inline in [Section 5](#5-experiments--observations).

---

## 1. Problem Background

### Why Does InnoDB Exist?

MySQL originally shipped with **MyISAM**: fast for reads, but with no transactions, no crash recovery, and only table-level locking. That is fine for a read-mostly website, catastrophic for anything that must not lose or corrupt committed data.

**InnoDB** (created by Innobase Oy, now the MySQL default since 5.5) was built to fix exactly that: a transactional, ACID, crash-safe, row-locking engine. Its design borrows heavily from classic Oracle/System-R techniques: a **clustered primary index**, **undo logs** for MVCC and rollback, and **redo logs** for durability.

The workload InnoDB is designed for is OLTP: Online Transaction Processing. Think e-commerce checkouts, bank transfers, user authentication. Millions of small, fast, concurrent transactions hitting the same tables. The engineering constraints this creates:

- **Reads must not block writes, and writes must not block reads**: or throughput collapses
- **Commits must be fast**: a user waiting 200ms for "order placed" is a bad experience
- **Crashes happen**: power cut mid-transaction must not corrupt data
- **Primary-key lookups must be near-instant**: these dominate OLTP query patterns

InnoDB's answer to all four is three architectural bets:

1. **Clustered storage**: the primary key B+Tree *is* the table; the row lives in the leaf page
2. **Undo-based MVCC**: old row versions live in a separate undo log, keeping the table compact
3. **Write-Ahead Redo Logging**: sequential redo records hit disk at commit; actual page writes happen later

Every other complexity in InnoDB traces back to these three decisions.

---

## 2. Architecture Overview

```
   SQL
    │
   MySQL server layer   (parser, optimizer, caches)
    │  handler API
    ▼
 ┌─────────────────────────────────────────────────────────────┐
 │  InnoDB storage engine                                        │
 │                                                               │
 │   ├── Buffer Pool ────────── caches data + index pages (LRU)  │
 │   │                                                           │
 │   ├── Clustered index ────── the table itself, keyed by PK    │
 │   ├── Secondary indexes ──── key → PK value                   │
 │   │                                                           │
 │   └── Transaction system                                      │
 │         ├── Undo logs  → old row versions (MVCC + rollback)   │
 │         ├── Redo log   → physical changes  (durability)       │
 │         └── Lock mgr   → row / gap / next-key locks           │
 └──────────┬──────────────────────┬───────────────┬─────────────┘
            ▼                      ▼               ▼
     .ibd tablespace          redo log         undo_001 / 002
     (clustered rows +        (fsync on        (old versions
      secondary indexes)       commit)          for MVCC)
```

### Components at a Glance

| Component | What It Solves |
|---|---|
| Clustered B+Tree | Primary-key lookups are O(log n) with zero extra heap fetch |
| Buffer Pool | Disk is 100,000x slower than RAM: cache hot pages |
| Undo Log | MVCC needs old row versions: store them outside the table |
| Redo Log | Commits must survive crashes: WAL before page flush |
| Lock Manager | Writers still conflict: row-level + gap locks |

### End-to-End: What Happens on `UPDATE users SET balance = balance - 100 WHERE id = 42`

```
Clustered B+Tree traversal finds row at leaf page
  → Buffer Pool loads that page (or reads from disk)
  → Transaction Manager creates undo record (old balance)
  → Row modified in-place inside buffer pool (new balance)
  → DB_TRX_ID and DB_ROLL_PTR updated in row header
  → Redo record generated (describes the change)
  → Row lock acquired on id=42
  → COMMIT: redo log flushed to disk (sequential write)
  → Client receives success
  → Dirty data page written to disk later by background flusher
  → Purge thread eventually discards undo record (when no txn needs it)
```

The philosophy: do the minimum at commit time (flush redo log), defer everything expensive (page writes, undo cleanup) to background processes.

**Write flow (durability + recovery):** a change modifies a page in the **buffer pool** (making it "dirty"), writes a **redo** record (flushed to disk on commit), and copies the *previous* row image into the **undo** log. Dirty pages are written back to the tablespace lazily. **Read flow (MVCC):** a consistent read follows `DB_ROLL_PTR` from the current row back through undo records to reconstruct the version visible to the reader's snapshot.

---

## 3. Internal Design

### 3.1 Clustered index = the table

The single most important design decision in InnoDB: **the primary key B+Tree leaf pages store the actual row data.** There is no separate heap file. The table's physical layout is determined by the primary key.

In InnoDB, the primary key B-tree *is* the table: leaf nodes contain the full rows, stored in PK order. Consequences:
- A **PK lookup** descends one B-tree straight to the row (no extra hop).
- Range scans also benefit: rows with adjacent primary keys are stored on adjacent pages, so range scans read pages sequentially.
- Every **secondary index** leaf stores the indexed column(s) **plus the primary key value** (not a physical pointer). So a secondary lookup is **two B-tree descents**: secondary index → PK value → clustered index → row. Unless the query is *covered* by the secondary index, it pays this back-lookup.

> Verified in [Experiment 1](#experiment-1--clustered-vs-secondary-index): a covered query reports "Covering index lookup", a non-covered one reports a plain "Index lookup" that implies the clustered back-lookup.

The flip side: random keys (UUIDs) mean random inserts throughout the B+Tree, causing frequent page splits and fragmentation. This is why InnoDB wants short, sequential, stable primary keys. `AUTO_INCREMENT` integers are ideal. UUIDs are not.

This is the **opposite** of PostgreSQL, whose tables are heap-organized and whose PK is just another secondary index pointing at heap `ctid`s (see [PostgreSQL Internals](../PostgreSQL_Internals/README.md)).

### 3.2 Undo logs: rollback + MVCC

Each row carries hidden columns `DB_TRX_ID` (last transaction that modified it) and `DB_ROLL_PTR` (pointer into the undo log). An **UPDATE happens in place**, but before overwriting, the old values are written to an **undo record**. Undo serves two jobs:
1. **Rollback**: if the transaction aborts, apply undo to restore the old image.
2. **MVCC consistent read**: an older transaction follows `DB_ROLL_PTR` chains to see the row *as of its snapshot*, without blocking the writer.

```
Before UPDATE:  [id=42, balance=5000, DB_TRX_ID=100, DB_ROLL_PTR=nullptr]
Undo record created:  "TRX 100 had balance=5000"
After  UPDATE:  [id=42, balance=4900, DB_TRX_ID=200, DB_ROLL_PTR → undo record]
```

When a reader needs the old version: it follows `DB_ROLL_PTR` to find the version visible to its snapshot.

Undo that no longer serves any open snapshot is removed by the **purge** thread; the **history list length** measures how much un-purged undo exists. A long-running transaction holding an old snapshot prevents purge: undo log grows, performance degrades. This is InnoDB's equivalent of PostgreSQL's dead-tuple bloat problem.

> Verified in [Experiment 5](#experiment-5--mvcc-consistent-read): under REPEATABLE READ, a reader kept seeing `balance=5000` even after another session committed `9999`: InnoDB rebuilt the old version from undo.

### 3.3 Redo logs: durability + crash recovery

The redo log is a **physical, circular** log of page changes. These two logs serve orthogonal purposes:

| | Undo Log | Redo Log |
|---|---|---|
| Question | "How do I go back?" | "How do I go forward after a crash?" |
| Used for | Rollback + MVCC snapshots | Crash recovery + durability |
| Written when | Before the row is modified | After the row is modified in memory |
| Discarded when | No active txn needs that version | After the corresponding page reaches disk |

On commit (`innodb_flush_log_at_trx_commit = 1`), redo up to the commit's LSN is `fsync`-ed: so commits wait only for a **sequential** log write, not for the (random) data-page writes. After a crash, InnoDB **replays redo** to roll dirty pages forward to the last committed state, then uses **undo** to roll back transactions that had not committed.

> Verified in [Experiment 2](#experiment-2--redo--undo-logs): redo capacity 100 MB, `flush_log_at_trx_commit = 1`, a live `Log sequence number`, and undo tablespaces `undo_001/002`.

### 3.4 Locking: row locks, gap locks, next-key locks

InnoDB locks **index records**, not table rows directly:
- **Record lock**: locks a single index row (e.g. `... WHERE id=42 FOR UPDATE`).
- **Gap lock**: locks the *gap between* index records, preventing inserts into a range.
- **Next-key lock**: record + the gap before it; this is the default under **REPEATABLE READ** and is how InnoDB prevents **phantom reads** without serializing everything.

Consider: `SELECT * FROM users WHERE age BETWEEN 20 AND 30 FOR UPDATE`. Without gap locks, another transaction could insert `age=25` right after this read, causing a phantom. Next-key locks block that insert.

> Verified in [Experiment 3](#experiment-3--row-level-locking) (two writers on different rows do not conflict) and [Experiment 4](#experiment-4--gap-locking) (an insert into a locked gap blocks).

### 3.5 Isolation levels

InnoDB defaults to **REPEATABLE READ** (snapshot fixed at first read, gap locks prevent phantoms). **READ COMMITTED** takes a fresh snapshot per statement and uses no gap locks (more concurrency, but non-repeatable reads). SERIALIZABLE turns plain `SELECT`s into locking reads.

---

## 4. Design Trade-Offs

### InnoDB vs. PostgreSQL: the central comparison

| | **InnoDB** | **PostgreSQL** |
|---|---|---|
| **Update strategy** | **In place**, old image → undo log | **Append** new tuple version, old left in heap |
| **MVCC source** | Reconstruct old versions from **undo** (Oracle-style) | Multiple **tuple versions** live in the heap |
| **Garbage collection** | **Purge** thread trims old undo | **VACUUM** reclaims dead tuples |
| **Table storage** | **Clustered** on PK (row lives in PK leaf) | **Heap** (PK is a separate index) |
| **Secondary index** | stores PK value → back-lookup needed | stores heap `ctid` → direct-ish |
| **Bloat location** | Undo log (separate, bounded by oldest snapshot) | The table itself (until VACUUM) |

Neither is universally better. InnoDB wins on primary-key access patterns and table compactness. PostgreSQL wins on simpler visibility logic and no undo-chain traversal for old snapshots.

| Trade-off | InnoDB benefit | InnoDB cost |
|---|---|---|
| Clustered index | Very fast PK range scans/lookups (data is local) | Secondary indexes pay a back-lookup; big PK = fat secondary indexes |
| In-place update + undo | Table stays compact; no VACUUM-style table bloat | Long-running readers grow undo / history list; purge lag |
| Gap/next-key locks | Phantom-free REPEATABLE READ without full serialization | Gap locks can block unrelated inserts; deadlock risk |
| Redo on commit | Fast, sequential commit fsync | Write amplification (redo + data + undo + binlog) |

---

## 5. Experiments / Observations

The dataset is 50k `users` (InnoDB); outputs below are quoted directly from the live runs.

### Experiment 1: Clustered vs secondary index
```
EXPLAIN ... WHERE id = 42        -> type=const, key=PRIMARY        (clustered: one descent)
EXPLAIN SELECT name,balance ... WHERE email=?  -> "Index lookup on users using idx_email"
                                                  (non-covering: + clustered back-lookup)
EXPLAIN SELECT id,email     ... WHERE email=?  -> "Covering index lookup using idx_email"
                                                  (covered: no back-lookup needed)
```
> **Insight:** `id, email` is fully answered inside the secondary index leaf (which stores `email → PK`), so it is a *covering* lookup. Asking for `name, balance` forces the second descent into the clustered index: the cost of clustered storage made visible.

### Experiment 2: Redo + undo logs
```
innodb_redo_log_capacity        = 104857600 (100 MB)
innodb_flush_log_at_trx_commit  = 1          (fsync redo on every commit)
Undo tablespaces: innodb_undo_001 (./undo_001), innodb_undo_002 (./undo_002)
SHOW ENGINE INNODB STATUS -> LOG: "Log sequence number 28025882", "Last checkpoint at 28025882"
```
> **Insight:** redo (durability) and undo (rollback + MVCC) are *physically separate* subsystems: two logs because they answer two different questions.

### Experiment 3: Row-level locking
```
Session A: BEGIN; SELECT ... WHERE id=42 FOR UPDATE;   (holds row lock)
Session B: UPDATE ... WHERE id=42  -> ERROR 1205 Lock wait timeout exceeded   (same row: blocked)
Session B: UPDATE ... WHERE id=43  -> success                                  (different row: free)
```
> **Insight:** the lock is on the **index record**, not the table: disjoint rows never conflict. (Contrast SQLite, where any second writer hits `database is locked`: see [Topic 1](../PostgreSQL_vs_SQLite/README.md).)

### Experiment 4: Gap locking
```
(deleted id=25000 to create a gap)
Session A (REPEATABLE READ): SELECT ... WHERE id BETWEEN 24990 AND 25010 FOR UPDATE;  (next-key locks)
Session B: INSERT id=25000  (inside locked gap) -> ERROR 1205 Lock wait timeout exceeded
Session B: INSERT id=40000  (outside range)     -> success
```
> **Insight:** the gap lock blocks an insert *into a range that was read FOR UPDATE*: this is how REPEATABLE READ prevents phantom rows without serializing the whole table.

### Experiment 5: MVCC consistent read (undo-based)
```
Session A (REPEATABLE READ): first read  balance = 5000
Session B commits           balance = 9999  (while A is open)
Session A: second read (same snapshot)  balance = 5000   <- UNCHANGED, served from undo
Session A: after COMMIT, fresh read     balance = 9999   <- now sees B's commit
```
> **Insight:** A's repeated read is stable because InnoDB reconstructed the pre-B version by following `DB_ROLL_PTR` into the undo log: non-blocking, snapshot-consistent reads, the InnoDB way (vs. PostgreSQL reading an older heap tuple version directly).

---

## 6. Key Learnings

1. **Clustered storage is InnoDB's defining choice.** "The PK B-tree *is* the table" explains both its fast PK access and the secondary-index back-lookup tax (Experiment 1).
2. **Two logs, two questions.** Undo answers "previous value?" (rollback + MVCC); redo answers "what survives a crash?" (durability). A transactional engine genuinely needs both, used together in recovery (Experiment 2).
3. **Locking is finer and richer than SQLite's, different from PostgreSQL's.** Record, gap, and next-key locks give row-level concurrency *and* phantom protection (Experiments 3 and 4).
4. **Same goal, opposite mechanism vs. PostgreSQL.** Both provide non-blocking MVCC reads, but InnoDB updates in place + keeps old versions in undo (purge cleans up), while PostgreSQL appends versions in the heap (VACUUM cleans up). InnoDB's choice keeps tables compact; PostgreSQL's keeps the write path simpler. Neither is strictly better: it is a trade-off (Experiment 5).
5. **Surprising observation:** a *long-running read* is what hurts InnoDB: it pins undo and grows the history list: whereas in PostgreSQL the analogous pain is dead-tuple bloat blocking VACUUM. Same root cause (an old snapshot must stay readable), different symptom.
6. **MVCC solves read-write conflicts but not write-write conflicts.** Readers see old versions without blocking. Writers modifying the *same* row must still serialize. InnoDB's gap locks add a further constraint: writers to the same index *range* can block each other even on different rows. Isolation level selection is a concurrency vs. consistency trade-off with real throughput implications.

---

## Architecture Reference Map

| Component | InnoDB Location | Key Structures |
|---|---|---|
| Clustered Index | `storage/innobase/btr/` | B+Tree traversal, page splits |
| Buffer Pool | `storage/innobase/buf/` | `buf_pool_t`, LRU young/old sublists |
| Undo Log | `storage/innobase/trx/trx0undo.cc` | `trx_undo_t`, undo record chains |
| Redo Log | `storage/innobase/log/` | `log_t`, WAL write + flush |
| MVCC Visibility | `storage/innobase/read/read0read.cc` | Snapshot creation, `ReadView` |
| Lock Manager | `storage/innobase/lock/` | Row locks, gap locks, next-key locks |

---

## References
- MySQL 9.x Reference Manual: *The InnoDB Storage Engine* (Clustered/Secondary Indexes, Redo/Undo Logs, InnoDB Locking and Transaction Model): https://dev.mysql.com/doc/refman/9.0/en/innodb-storage-engine.html
- *InnoDB Locking* and *Consistent Nonlocking Reads*: https://dev.mysql.com/doc/refman/9.0/en/innodb-locking.html
- MySQL source: `storage/innobase/`
- Comparison context: PostgreSQL 16 *Concurrency Control* docs
- Hellerstein, Stonebraker, Hamilton: *Architecture of a Database System*

*All experiment outputs above were produced by running MySQL 9.6.0 (InnoDB) locally. Original work.*
