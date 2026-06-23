# MySQL / InnoDB — Clustered Storage, Undo + Redo, and a Different Take on MVCC

**Name:** Parth Sankhla
**Roll Number:** 24BCS10229

I came to InnoDB straight after studying PostgreSQL's internals, and that order helped a lot, because InnoDB keeps asking the same questions Postgres does and answering several of them differently. Both want ACID transactions, both cache pages in a buffer pool, both do MVCC — but InnoDB stores the table *inside* its primary key, updates rows *in place*, and keeps two separate logs instead of one. Watching the same problems get solved a second way is what made the trade-offs visible to me, so I'll lean on that comparison throughout.

---

## 1. Problem Background

InnoDB is the default storage engine in modern MySQL, and the reason it became the default is exactly the reason MyISAM (the old default) faded: MyISAM had no transactions, no crash recovery, and only table-level locking. For anything resembling a real transactional workload — money, orders, concurrent users — that's a non-starter.

So InnoDB exists to give MySQL the things a serious database needs: ACID transactions, crash recovery, row-level locking so two users editing different rows don't fight, and MVCC so readers don't block writers. The interesting design choice is that it builds all of this around a **clustered index**, which shapes everything else.

---

## 2. Architecture Overview

```
        SQL  (mysqld server layer: parser, optimizer)
                       |
                       v
        +-------------------------------+
        |        InnoDB engine          |
        |                               |
        |  Buffer Pool (cached pages)   |
        |  Change Buffer                |
        |                               |
        |  Clustered index (B+tree)     |  <- the table itself
        |    leaf pages = full rows     |
        |  Secondary indexes (B+trees)  |  <- leaf = PK value
        +---------------+---------------+
                        |
          +-------------+--------------+
          |             |              |
      Redo log      Undo log      Doublewrite buffer
   (durability,   (rollback +    (torn-page
    roll forward)  MVCC versions)  protection)
                        |
                        v
                  data files (.ibd)
```

The shape to notice: the **table is a B+tree**, not a heap with indexes bolted on. The clustered index's leaf level *is* the row data, ordered by primary key. Secondary indexes are separate B+trees, but their leaves store the **primary key value** rather than a physical row pointer. That one detail explains a surprising amount of InnoDB's behavior.

---

## 3. Internal Design

### Clustered index — the table lives inside the primary key

This was the biggest mental shift coming from Postgres, where a table is an unordered heap. In InnoDB, the rows are physically stored at the leaves of the primary key's B+tree, in key order. So "find the row with PK = 42" is just one B+tree descent that lands directly on the full row. No separate heap fetch.

The consequence shows up in **secondary indexes**. Because a secondary index leaf stores the PK value (not a physical address), looking something up by a secondary key means *two* traversals: walk the secondary B+tree to get the PK, then walk the clustered B+tree to get the actual row. Postgres's index-then-heap-fetch costs one index descent plus one direct page read; InnoDB's secondary lookup costs two full B+tree descents. There's a reason for it — if rows physically move (and in a clustered index they do, as pages split), a stored physical pointer would constantly go stale, whereas a PK value never does.

### Undo logs and MVCC — versions without bloating the table

InnoDB does MVCC, but the opposite way to Postgres. Postgres writes a brand-new tuple version on every update and leaves the old one in the table for VACUUM to clean. InnoDB **updates the row in place** and pushes the *old* version into the **undo log**. When an older transaction needs to see the previous value, InnoDB walks the undo log to reconstruct the version that was valid for that transaction's read view.

So the table stays compact (no dead tuples sitting in the heap), and there's no VACUUM. The cost moves to the undo log instead — a long-running read transaction forces InnoDB to keep old undo records around to serve that transaction's view, and that history can grow.

### Redo log — durability and crash recovery

The redo log is InnoDB's write-ahead log: before a page change is finalized on disk, the change is recorded in the redo log and flushed. After a crash, InnoDB **rolls forward** by replaying redo from the last checkpoint, so committed changes survive. This is the same idea as Postgres's WAL.

### Why both undo *and* redo

This confused me until I separated their jobs:

- **Redo** answers "a committed transaction must not be lost." On restart it *rolls forward* — reapplies committed changes that hadn't reached the data files yet.
- **Undo** answers two different things: "an uncommitted transaction must be reversible" (rollback, and roll-back of in-flight transactions during recovery) and "older readers must still see old versions" (MVCC).

They point in opposite directions in time — redo reconstructs the future state, undo reconstructs the past state — which is why one log can't do both.

### Locking — row locks, gap locks, next-key locks

InnoDB locks at the **row** level, not the table. Beyond plain row locks it has **gap locks** (locking the space *between* index records) and **next-key locks** (a row lock plus the gap before it). These exist to stop **phantom reads** under the REPEATABLE READ isolation level: if a query read "all rows where age = 25," a gap lock prevents another transaction from inserting a *new* age-25 row into that range before the first transaction finishes.

---

## 4. Design Trade-Offs (mostly vs PostgreSQL)

| Concern | InnoDB | PostgreSQL |
|---|---|---|
| Table storage | clustered B+tree (rows in PK order) | unordered heap |
| Update strategy | in place + undo log | new tuple version |
| Old versions live in | undo log | the table itself (until VACUUM) |
| Cleanup | purge of undo history | VACUUM of dead tuples |
| PK lookup | one descent, lands on the row | index descent + heap fetch |
| Secondary lookup | two descents (secondary -> clustered) | index descent + heap fetch |

The honest reading of this table: neither model is free.

- **Clustered storage** makes primary-key range scans fast and keeps the row right where the PK lookup lands, but it taxes every secondary-index lookup with a second traversal, and choosing a bad primary key (random UUIDs, say) causes page splits and fragmentation.
- **In-place updates + undo** keep the table dense and avoid VACUUM, but shift the burden to undo history, which a long-running transaction can balloon.
- **Postgres's append model** never makes a secondary lookup pay double and keeps writers simple, but it accepts table bloat and a permanent VACUUM habit.

It's the same conservation law each time: someone has to store the old version and someone has to clean it up; the engines just disagree on where to put that work.

---

## 5. Experiments / Observations

I don't have lab measurements for InnoDB the way I do for Postgres (Lab 2), so instead I set up small, reproducible experiments and reasoned about the structure they expose, rather than quoting benchmark numbers I didn't measure.

**Experiment 1 — see the clustered vs secondary cost with EXPLAIN.** On a table with a primary key and a secondary index:

```sql
EXPLAIN SELECT * FROM users WHERE id = 42;          -- PK lookup
EXPLAIN SELECT * FROM users WHERE email = 'x@y.z';  -- secondary lookup
```

The PK query reports access through the clustered index (`PRIMARY`), and the row is right there at the leaf. The secondary query reports the secondary index, but conceptually it then has to follow the stored PK back into the clustered index for the full row. If I `SELECT` only columns already contained in the secondary index, EXPLAIN shows `Using index` (a covering index) and the second traversal disappears — which is direct confirmation that the second descent is about fetching the rest of the row.

**Experiment 2 — watch a gap lock block a phantom.** In two sessions under REPEATABLE READ:

```sql
-- session A
START TRANSACTION;
SELECT * FROM users WHERE age = 25 FOR UPDATE;   -- takes next-key/gap locks

-- session B
START TRANSACTION;
INSERT INTO users (age) VALUES (25);             -- blocks until A commits
```

Session B's insert *waits*, even though it isn't touching any existing row, because A's gap lock owns the range. That blocking is the phantom-prevention mechanism doing its job, and `SHOW ENGINE INNODB STATUS` lists the lock waits so you can see it explicitly.

**Experiment 3 — observe MVCC via undo.** Start a transaction in session A and `SELECT` a row, then `UPDATE` that same row in session B and commit. Session A, still inside its transaction, keeps seeing the **old** value — InnoDB reconstructed it from the undo log for A's read view — while a fresh connection sees the new value. Same row, two truths, decided by undo history.

These are qualitative on purpose: each one is something you can run and watch happen, and each isolates one design decision (clustered storage, gap locking, undo-based MVCC) rather than collapsing into a single benchmark figure.

---

## 6. Key Learnings

- **Clustered index is the keystone.** Once I accepted "the table *is* the primary key B+tree," the two-traversal secondary lookup, the importance of a good PK, and the fast PK range scans all fell out of that single fact.
- **Undo and redo are not redundant.** Redo rolls forward to save committed work; undo rolls back uncommitted work and serves old MVCC versions. Opposite directions in time, hence two logs.
- **MVCC has more than one implementation.** InnoDB (in-place + undo) and Postgres (new versions + VACUUM) reach the same "readers don't block writers" guarantee by moving the cost to completely different places.
- **Locking is about phantoms, not just rows.** Gap and next-key locks only make sense once you see them as protecting *ranges* to stop phantom inserts under REPEATABLE READ.
- **Studying the second system taught me the first.** A lot of what I understood about Postgres only locked in when I saw InnoDB make the opposite choice and pay a different bill.
