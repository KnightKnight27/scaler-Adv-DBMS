# MySQL / InnoDB Storage Engine

**Author:** Shubham Shah · **Roll No:** 24BCS10316 · **Course:** Advanced DBMS, Scaler School of Technology
**Topic 3: InnoDB** Clustered indexes · Buffer Pool · Undo/Redo logs · Row & Gap locking

---

## 1. Problem Background

MySQL began with **MyISAM**, a fast but **non-transactional** engine: no crash
recovery, table-level locking, and torn writes on failure. As MySQL moved into
banking, e-commerce, and other workloads that cannot lose committed data, MyISAM's
lack of **ACID** guarantees became disqualifying.

**InnoDB** (created by Heikki Tuuri, acquired by Oracle, now MySQL's default since
5.5) was built to provide exactly what MyISAM lacked:

- **Transactions with ACID guarantees**, commit must be durable, crashes must
  recover to a consistent state.
- **High concurrency without table locks**, many writers on the same table
  through **row-level locking**.
- **MVCC** so reads don't block writes, borrowed from the Oracle model
  (old versions reconstructed from **undo logs**, not stored inline).
- **Crash safety** via **redo logging** and careful page-flush ordering.

The defining structural decision is the **clustered index**: InnoDB physically
stores each table's rows *inside* its primary-key B+ tree. This one choice shapes
its performance profile, its secondary-index design, and its trade-offs versus
PostgreSQL's heap model.

---

## 2. Architecture Overview

InnoDB is a storage engine *inside* the MySQL server process. SQL handling
(parser, optimizer) is shared MySQL infrastructure; InnoDB plugs in beneath it and
owns storage, indexing, transactions, and recovery.

```
   client ──SQL──► MySQL server (parser ▸ optimizer ▸ executor)
                              │  handler API
                              ▼
   ┌──────────────────── InnoDB storage engine ─────────────────────┐
   │                                                                 │
   │   ┌──────────────────── IN-MEMORY ──────────────────────┐       │
   │   │  Buffer Pool   (data + index pages, LRU)            │       │
   │   │  Change Buffer · Adaptive Hash Index               │       │
   │   │  Log Buffer (redo, in RAM)                          │       │
   │   └──────────┬───────────────────────────┬─────────────┘       │
   │              │ flush dirty pages          │ flush redo (commit)  │
   │              ▼                            ▼                       │
   │   ┌────────────────────┐      ┌────────────────────────┐         │
   │   │  Tablespaces       │      │  Redo log (ib_logfile / │         │
   │   │  *.ibd  (B+ trees: │      │  #innodb_redo)          │         │
   │   │  clustered + 2ndary│      └────────────────────────┘         │
   │   │  indexes, undo segs)│     ┌────────────────────────┐         │
   │   └────────────────────┘      │  Undo logs (rollback   │         │
   │                               │  segments, in system/  │         │
   │                               │  undo tablespaces)     │         │
   │                               └────────────────────────┘         │
   └─────────────────────────────────────────────────────────────────┘
   background threads: page cleaner (flush) · purge · master · I/O
```

**Data flow for an UPDATE.** Locate the row via the clustered index → take a
row **X-lock** → write the *before-image* to an **undo log** (for rollback + MVCC)
→ modify the page **in place** in the buffer pool (page now dirty) → append a
**redo** record to the log buffer → on `COMMIT`, flush redo to disk (durability).
The dirty data page is written back **later** by the page-cleaner thread.

---

## 3. Internal Design

### 3.1 Clustered Index: the table *is* a B+ tree

In InnoDB, the **primary key B+ tree leaves contain the full rows**. There is no
separate heap; the table's physical order *is* primary-key order.

```
   Clustered index (PRIMARY KEY = id)
                 [ • 40 • 80 • ]
                 /      |       \
        [id 10..39]  [id 40..79]  [id 80..]      ← leaf pages hold WHOLE rows
        id=10 → (full row: name,bal,…)
        id=11 → (full row …)            leaves linked L→R for range scans
```

- **Primary-key lookups are one tree traversal** that lands directly on the row,
  no second hop. Range scans on the PK walk contiguous leaf pages (great locality).
- **If no PRIMARY KEY is declared,** InnoDB uses the first non-null UNIQUE index,
  or synthesizes a hidden 6-byte `DB_ROW_ID`. A clustered index always exists.

**Secondary indexes store the PK, not a row pointer.** A secondary index's leaf
entry is `(secondary_key → primary_key)`. Looking up a row via a secondary index
therefore costs **two B+ tree traversals**: one in the secondary index to get the
PK, then one in the clustered index to fetch the row (a "bookmark lookup").

```
   Secondary index (KEY on email)
        email='a@x' → PK id=10      ──► then descend clustered index to id=10
```

This is a deliberate trade: because secondary indexes reference the **logical** PK
rather than a **physical** location, rows can move (page splits, reorganizations)
without touching every secondary index. The cost is the extra clustered-index
lookup. (Mitigation: **covering indexes**. If the secondary index already
contains all selected columns, the second hop is skipped.)

Every clustered-index row also carries hidden system columns: **`DB_TRX_ID`** (last
transaction that modified it) and **`DB_ROLL_PTR`** (pointer to its undo record),
the machinery for MVCC below.

### 3.2 Buffer Pool

InnoDB's **buffer pool** (`innodb_buffer_pool_size`, often 50–75% of RAM) caches
16 KB pages, both data and index, since they're the same B+ trees. All reads and
writes go through it.

- **Replacement: a midpoint-insertion LRU.** The LRU list is split into a
  **"young" (hot)** sublist and an **"old" (cold)** sublist. A newly read page is
  inserted at the **head of the old sublist (the midpoint)**, *not* the very head.
  Only if it is accessed *again* after a short delay is it promoted to the young
  sublist. **Why:** a big one-time scan (e.g. a backup reading every page) floods
  the pool with pages that are never reused; midpoint insertion keeps them in the
  cold zone where they're evicted quickly, protecting the genuinely hot working
  set from being flushed out. (PostgreSQL solves the same scan-pollution problem
  with clock-sweep + ring buffers; InnoDB uses the two-segment LRU.)
- **Change Buffer.** Modifications to *non-unique secondary index* pages that
  aren't in the pool are buffered and merged later, turning random secondary-index
  writes into sequential ones.
- **Adaptive Hash Index.** InnoDB watches access patterns and builds an in-memory
  hash over hot B+ tree pages, short-circuiting tree descents for frequent lookups.
- **Dirty pages** are flushed by **page-cleaner** threads, paced to keep redo-log
  space available and avoid I/O spikes.

### 3.3 MVCC via Undo Logs (in-place updates)

InnoDB also gives readers a consistent snapshot without blocking writers, but its
mechanism is the **opposite of PostgreSQL's**. Instead of leaving old versions in
the table, InnoDB **updates the row in place** and pushes the **before-image into
an undo log**. Old versions are *reconstructed on demand* by walking undo records.

```
   UPDATE accounts SET bal=900 WHERE id=1;   (trx 105)

   clustered-index row (in place):
        id=1, bal=900   DB_TRX_ID=105   DB_ROLL_PTR ─┐
                                                     ▼
   undo log:  [ before-image: bal=1000, prev DB_TRX_ID=100 ]  (a version chain)

   A reader with an older snapshot follows DB_ROLL_PTR to see bal=1000.
```

- **Read view (snapshot).** A consistent read builds a *read view* listing the
  transactions active at its start. When it meets a row whose `DB_TRX_ID` is too
  new to be visible, it follows `DB_ROLL_PTR` down the undo chain until it finds a
  version it *is* allowed to see. This is **REPEATABLE READ** (MySQL's default),
  and it makes reads completely lock-free.
- **Rollback** replays the undo records in reverse to restore before-images.
- **Purge.** A background **purge** thread permanently discards undo records once
  no read view can still need them, InnoDB's analogue of PostgreSQL's VACUUM, but
  it cleans a *separate* undo area rather than the table itself, so the table
  doesn't bloat from dead row versions.

### 3.4 Redo Log & Durability (WAL)

InnoDB is write-ahead-logged. Before a dirty page reaches disk, the **redo log**
records the change.

- **Redo log** = a fixed-size **circular** file set recording *physical* page
  changes ("page P, offset O, new bytes B"). On `COMMIT`, redo up to that
  transaction is fsynced (controlled by `innodb_flush_log_at_trx_commit=1` for full
  ACID). Sequential log writes are cheap; the scattered data pages are flushed
  lazily, the classic WAL decoupling.
- **LSN.** A monotonically increasing **Log Sequence Number** tags every change
  and every page; recovery uses it to know which changes a page already reflects.
- **Crash recovery = REDO then UNDO.** On restart InnoDB (1) **redoes** all logged
  changes from the last checkpoint forward, reconstructing committed-but-unflushed
  pages, then (2) **undoes** transactions that were in flight at the crash, using
  the undo logs. (PostgreSQL needs only REDO because uncommitted versions are just
  never made visible; InnoDB updates in place, so it *must* roll them back.)
- **Doublewrite buffer.** Because a 16 KB page write isn't atomic on disk, InnoDB
  first writes the page to a **doublewrite** area, then to its real location. A
  crash mid-write leaves a clean copy in one of the two, protection against
  **torn pages**.

### 3.5 Locking: Row, Gap, and Next-Key

InnoDB locks at **row granularity** (recorded in the lock table, not on the page),
so concurrent writers to different rows of the same table don't conflict.

- **Shared (S) / Exclusive (X) record locks** on individual index records.
- **Gap locks** lock the *open interval between* index records, locking a range
  where no row exists yet.
- **Next-key lock** = record lock **+** the gap before it. This is how InnoDB's
  default REPEATABLE READ prevents **phantom rows**: a ranged `SELECT … FOR UPDATE`
  locks not just matching rows but the gaps, so another transaction cannot
  *insert* a new row that would change the result set.

```
   existing ids:  10        20        30
   gaps:        (..10)  (10,20)  (20,30)  (30,..)

   SELECT ... WHERE id BETWEEN 15 AND 25 FOR UPDATE
   → next-key locks covering rows 20 and the gaps (10,20],(20,30)
   → a concurrent INSERT id=22 BLOCKS (phantom prevented)
```

Gap/next-key locking is what lets InnoDB offer **REPEATABLE READ with no
phantoms** (stronger than the SQL-standard requirement), but it can also produce
deadlocks and surprising blocking; InnoDB runs automatic **deadlock detection**
and aborts the cheapest victim.

---

## 4. Design Trade-Offs

| Decision | Advantage | Cost / Limitation |
|----------|-----------|-------------------|
| **Clustered index (row in PK tree)** | PK lookups & PK range scans are extremely fast (one traversal, great locality) | Secondary lookups need 2 traversals; **large PKs bloat every secondary index** (they all embed the PK) → prefer small monotonic PKs |
| **Secondary index → PK (logical pointer)** | Rows can move without touching secondary indexes; stable under page splits | Extra clustered lookup per secondary hit (unless covering index) |
| **In-place update + undo log** | Table stays compact (no dead-version bloat); MVCC with separate, purgeable undo | Update path is heavier (write undo + redo + page); long transactions grow undo history (history-list bloat); recovery needs UNDO pass |
| **Redo log (circular) + doublewrite** | Fast sequential commit; torn-page protection | Doublewrite ~doubles page-write I/O; redo size caps in-flight work |
| **Row + gap/next-key locks** | High write concurrency; phantom-free REPEATABLE READ | Gap locks reduce concurrency on ranges; deadlock potential; subtle blocking |
| **Midpoint-insertion LRU** | Scan-resistant buffer pool | More complex than plain LRU; needs tuning (`innodb_old_blocks_*`) |

**InnoDB vs PostgreSQL MVCC, the central comparison:**

| | PostgreSQL | InnoDB |
|---|---|---|
| Update | new tuple version in the **heap** (no overwrite) | **in-place**, old image to **undo log** |
| Old versions live in | the table itself | separate undo segments |
| Cleanup | **VACUUM** (cleans the table) | **purge** (cleans undo) |
| Table layout | heap + all-secondary indexes | **clustered** by PK |
| Recovery | REDO only | REDO **+** UNDO |
| Bloat shows up as | dead tuples in the table | long undo history list |

Why did PostgreSQL choose differently? No-overwrite makes rollback and snapshot
isolation conceptually simple and keeps the write path light, at the cost of
table bloat + VACUUM. InnoDB keeps tables compact and clustered (fast PK access)
at the cost of undo-log management and a heavier update path. **Neither is "more
correct": they relocate the same MVCC cost to different places.**

---

## 5. Experiments / Observations

**See the clustered vs secondary cost difference (`EXPLAIN` / handler counters):**

```sql
-- PK lookup: single clustered traversal
EXPLAIN SELECT * FROM accounts WHERE id = 42;          -- type: const / eq_ref

-- Secondary lookup: index → PK → clustered (two hops)
EXPLAIN SELECT * FROM accounts WHERE email = 'a@x.com';-- key: email_idx; then bookmark lookup

-- Covering index avoids the second hop entirely
EXPLAIN SELECT email FROM accounts WHERE email = 'a@x.com'; -- Extra: "Using index"
```

Use `SHOW SESSION STATUS LIKE 'Handler_read%';` around each query to *count* the
read operations, the covering query shows far fewer `Handler_read_next` calls.

**Observe locking and MVCC directly:**

```sql
-- Session A
START TRANSACTION;
SELECT * FROM accounts WHERE id BETWEEN 15 AND 25 FOR UPDATE;  -- takes next-key locks
-- Session B (blocks on a phantom insert)
INSERT INTO accounts(id) VALUES (22);     -- waits → gap lock proven

-- Inspect live locks / waits:
SELECT * FROM performance_schema.data_locks;
SELECT * FROM performance_schema.data_lock_waits;
```

**Engine-wide internals to inspect:**

- `SHOW ENGINE INNODB STATUS\G` → buffer-pool hit rate, last deadlock, pending
  flushes, **history list length** (undo backlog, InnoDB's "bloat" signal).
- `SELECT @@innodb_buffer_pool_size, @@innodb_page_size;` → confirm 16 KB pages.
- Repeatedly UPDATE one row inside a long-open transaction in another session and
  watch the **history list length** grow (undo can't be purged), the InnoDB
  analogue of PostgreSQL bloat under a long transaction.

---

## 6. Key Learnings

- **The clustered index is the keystone.** "The table *is* the primary-key B+
  tree" explains nearly everything downstream: blazing PK access, two-hop
  secondary lookups, the danger of large PKs, and why secondary indexes store the
  PK rather than a physical address.
- **Undo and redo are not redundant: they answer opposite questions.** Redo says
  *"redo committed work the crash lost"* (durability); undo says *"undo
  uncommitted work the crash left behind"* and *"reconstruct old versions for
  readers"* (atomicity + MVCC). InnoDB needs both precisely because it updates
  in place.
- **MVCC is a choice of *where to pay*, not whether.** InnoDB pays in undo
  management and a heavier write path to keep tables compact; PostgreSQL pays in
  VACUUM to keep the write path light. Studying both made the trade-off concrete.
- **Locking can be stronger than the standard.** Next-key locks give InnoDB
  phantom-free REPEATABLE READ, which is convenient, but the gap-locking that enables it is
  also the source of its trickiest deadlocks.
- **Surprising takeaway:** an oversized or random PRIMARY KEY (e.g. a UUID)
  silently inflates *every* secondary index and scatters inserts across the
  clustered tree, a single schema decision with repo-wide storage and I/O cost.
