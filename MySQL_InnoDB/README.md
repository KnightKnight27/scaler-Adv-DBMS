# MySQL / InnoDB Storage Engine

InnoDB is MySQL's default storage engine, and its design reads almost like a point-by-point
counterargument to PostgreSQL. Where Postgres appends new row versions and cleans up later,
InnoDB updates rows **in place** and keeps the old versions in a separate **undo log**. That
one decision — in-place updates — explains the clustered-index layout, the need for two
different logs, and the whole locking story. Worth keeping in mind throughout.

---

## 1. Problem Background

InnoDB was created by Heikki Tuuri (Innobase Oy, ~1995, later acquired by Oracle) to give
MySQL something it badly lacked: a **transactional, crash-safe, row-locking** engine. The
older MyISAM engine was fast for reads but had table-level locking and no transactions. InnoDB
brought ACID, MVCC, foreign keys, and fine-grained locking — and became the default in MySQL
5.5. Its model is consciously **Oracle-style**: in-place updates plus undo segments for
read-consistency.

---

## 2. Architecture Overview

```
              SQL  -> parser/optimizer  -> InnoDB handler
                                               |
        +--------------------------------------+--------------------------+
        |                  Buffer Pool (in memory)                        |
        |   data + index pages, LRU (young/old), change buffer            |
        +-----------------------------------------------------------------+
              |                 |                    |
        clustered index    secondary indexes     undo pages (in
        (B+tree = table)    (B+tree -> PK)        system/undo tablespace)
              |                 |
              v                 v
        +-----------------------------------------------------------------+
        |   On disk:  .ibd tablespaces  |  redo log (ib_logfile, circular) |
        |             doublewrite buffer | undo logs                        |
        +-----------------------------------------------------------------+
```

The **buffer pool** is the heart of performance; the **redo log** guarantees durability; the
**undo log** powers rollback and MVCC. Data and indexes are both B+trees living in
tablespaces.

---

## 3. Internal Design

### Clustered index — the table *is* a B+tree

In InnoDB, the table is physically stored **as** its primary-key B+tree. The leaf nodes don't
point to the rows — they *contain* the rows. So a primary-key lookup walks the tree and finds
the full row right there, no second fetch.

```
   Clustered index (PRIMARY KEY = id)
        [ internal nodes: separator keys ]
                     |
        leaf pages ordered by id, holding FULL rows:
        | id=1 full-row | id=2 full-row | id=3 full-row | ...
```

**Secondary indexes** are separate B+trees whose leaves store the indexed column(s) **plus the
primary-key value** — not a physical row pointer. So a query filtered on a secondary index does
two lookups: find the PK in the secondary tree, then walk the clustered tree to fetch the row.
(Unless the index is *covering* — all needed columns are in the index itself — in which case the
second lookup is skipped.)

This is also why InnoDB strongly prefers a small, monotonically increasing PK (e.g.
`AUTO_INCREMENT`). The PK value is copied into *every* secondary index, and random PK inserts
scatter writes across the clustered tree, causing page splits and fragmentation. Sequential
keys append cleanly to the rightmost leaf.

### Buffer pool

Pages are cached in the buffer pool with an **LRU that has a twist**: the list is split into a
"young" (hot) and "old" sublist, and newly read pages enter at the **midpoint**, not the head.
A big table scan therefore fills the old sublist and gets evicted quickly, instead of flushing
your genuinely hot working set out of cache. The **change buffer** further optimizes writes to
*non-unique secondary indexes* by buffering them in memory and merging them lazily, avoiding
random I/O on pages that aren't cached.

### Undo logs, redo logs — and why you need both

This trips people up, so it's worth being precise. They run in **opposite directions**:

- **Redo log** = *roll forward.* A physical-ish record of "this change was made to this page."
  On `COMMIT`, redo is flushed (write-ahead logging). After a crash, InnoDB replays redo to
  reapply committed changes that hadn't yet been flushed from the buffer pool to disk. This is
  the **durability** mechanism. The redo log is a fixed-size **circular** file.
- **Undo log** = *roll back.* The *previous* version of each row you modify. It serves two jobs:
  (1) if the transaction aborts (or crashes uncommitted), undo restores the old values; and
  (2) it provides **MVCC** — a reader needing an older snapshot reconstructs the prior row
  version by walking the undo chain.

So: redo lets you keep changes you *did* commit; undo lets you discard changes you *didn't*,
and lets others read the old value meanwhile. You need both because a crash can leave you with
*both* committed-but-unflushed changes (redo) *and* uncommitted-but-flushed changes (undo) at
the same time. The **doublewrite buffer** sits alongside redo to guard against torn (partial)
page writes.

### MVCC, in-place

Each clustered-index row carries hidden columns `DB_TRX_ID` (last transaction to modify it) and
`DB_ROLL_PTR` (a pointer to the undo record holding the previous version). An UPDATE overwrites
the row **in place**, but first stashes the old image in undo and links to it. A reader with an
older read-view follows `DB_ROLL_PTR` back through undo until it finds a version visible to its
snapshot. A background **purge** thread eventually deletes undo records once no active
transaction could possibly need them.

### Locking and isolation

InnoDB locks at the **row** level, and it does so on **index records**. Beyond plain record
locks it has:

- **Gap locks** — lock the *gap between* index records so no one can insert there.
- **Next-key locks** — a record lock + the gap before it; the default under REPEATABLE READ,
  used to stop **phantom rows** in range queries.

Default isolation is **REPEATABLE READ**, where a transaction takes one consistent snapshot
(via undo) for all plain reads. **READ COMMITTED** takes a fresh snapshot per statement and uses
fewer gap locks. SERIALIZABLE turns plain reads into locking reads.

---

## 4. Design Trade-Offs

**Clustered index — pros:** PK lookups and PK range scans are extremely fast (data is right
there, in physical order). **Cons:** every secondary index is fatter (carries the PK) and
secondary lookups pay a second probe; a poorly chosen or random PK fragments the whole table.

**In-place + undo vs Postgres's append + VACUUM.** InnoDB keeps the table compact and reads of
the *current* row are direct — no bloat to vacuum. The cost is undo-log management and a purge
thread, plus long-running transactions that pin old undo can cause the **history list** to grow
and slow everyone down (InnoDB's version of "bloat pressure"). PostgreSQL's opposite choice
makes rollback trivial (just don't show the new tuple) but needs VACUUM. Neither is free —
they just move the cost around. **This is the central PG-vs-InnoDB MVCC trade-off.**

**Redo log size** is a classic tuning knob: bigger redo = fewer, less frequent checkpoints and
better write throughput, but longer crash recovery.

---

## 5. Experiments / Observations

- **See the double lookup.** Create a table with a secondary index, then compare
  `EXPLAIN` for a query covered by the index versus one needing extra columns. The covered query
  shows `Using index`; the other doesn't — the difference is that second probe into the
  clustered index. You can watch `Handler_read_*` counters in `SHOW STATUS` move accordingly.
- **Sequential vs random PK.** Bulk-insert with `AUTO_INCREMENT` vs random UUID PKs and compare
  table size / insert throughput. Random PKs fragment the clustered tree and inflate the
  `.ibd` file — a very tangible demonstration of why clustered storage rewards monotonic keys.
- **Undo pressure.** Open a long transaction in one session, churn updates in another, and watch
  the **history list length** climb in `SHOW ENGINE INNODB STATUS`. It only drops once the old
  transaction ends and purge catches up — undo's cost made visible.
- **Gap locks.** In REPEATABLE READ, two sessions running `SELECT ... FOR UPDATE` on the same
  range will block each other even on rows that don't exist yet — that's the next-key lock
  stopping phantoms.

---

## 6. Key Learnings

- **In-place updates are the root decision.** They give you a compact, clustered table with no
  vacuum — but they *force* the undo log into existence, both for rollback and for MVCC reads.
- **Redo and undo are not redundant; they're complementary.** Redo rolls committed work forward
  after a crash (durability); undo rolls uncommitted work back and serves old snapshots (atomicity
  + isolation).
- **Clustered indexes are a sharp tool.** They make PK access wonderful and secondary access a
  little more expensive, and they make your choice of primary key a real performance decision —
  pick small and monotonic.
- Compared to PostgreSQL, InnoDB simply **chose to pay the MVCC cost up front** (managing undo and
  purge) rather than after the fact (VACUUM). Same correctness goal, opposite engineering route.
