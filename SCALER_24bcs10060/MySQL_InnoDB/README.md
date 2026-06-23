# MySQL / InnoDB Storage Engine

> Advanced DBMS — System Design Discussion
> Topic 3: MySQL / InnoDB

InnoDB is the default storage engine in MySQL. After studying PostgreSQL in the
previous topic, I found InnoDB interesting because it solves the *same* problems
(concurrency, durability, fast lookups) but makes **different choices** —
especially clustered indexes and undo logs. This README focuses on those choices
and compares them to PostgreSQL where it helps me understand.

---

## 1. Problem Background

MySQL itself is a database server, but it splits into two layers:
- The **MySQL server layer** (parser, optimizer, connection handling).
- The **storage engine** layer, which actually stores and retrieves rows.

MySQL can use different storage engines, but **InnoDB** is the default one because
it gives **transactions (ACID)**, **crash recovery**, and **row-level locking**.
An older engine, MyISAM, was faster for simple reads but had no transactions and
only table-level locks, so InnoDB became the standard for real applications.

So InnoDB exists to be the **reliable, transactional, multi-user** storage engine
for MySQL.

---

## 2. Architecture Overview

```
            Clients
              |
   +----------------------+
   |   MySQL Server layer |   parser / optimizer / SQL
   +----------+-----------+
              |  (storage engine API)
              v
   +--------------------------------------------------+
   |               InnoDB Storage Engine              |
   |                                                  |
   |  In memory:                                      |
   |   +------------------+   +-------------------+    |
   |   |   Buffer Pool    |   |  Log buffer       |    |
   |   | (caches pages)   |   | (for redo log)    |    |
   |   +--------+---------+   +---------+---------+    |
   |            |                       |              |
   +------------|-----------------------|--------------+
                v                       v
        +---------------+        +---------------+
        |  Data files   |        |  Redo log     |
        |  (.ibd):      |        |  (ib_logfile) |
        |  clustered    |        +---------------+
        |  B-tree +     |        +---------------+
        |  secondary    |        |  Undo logs    |
        |  indexes      |        |  (old row     |
        +---------------+        |   versions)   |
                                 +---------------+
```

Main components:
- **Buffer Pool** — InnoDB's in-memory cache of data/index pages (like PostgreSQL's
  shared buffers).
- **Clustered index** — the table data itself, stored sorted by primary key.
- **Secondary indexes** — extra indexes that point back using the primary key.
- **Redo log** — for durability / crash recovery.
- **Undo log** — for rollback and for MVCC (reading old versions).

---

## 3. Internal Design

### 3.1 Clustered index (the big idea)

In InnoDB, **the table *is* the primary-key index.** The rows are physically
stored **inside a B-tree, sorted by the primary key**, and the *leaf pages of that
B-tree contain the actual full rows* (not just pointers). This is called a
**clustered index**.

```
Clustered index (= the table, sorted by primary key)

                 [ root ]
                /        \
          [internal]   [internal]
           /     \
     [leaf: id=1 FULL ROW][id=2 FULL ROW] ... [leaf: id=99 FULL ROW]
        ^ leaves store the real row data, in primary key order
```

Compare to PostgreSQL where the table (heap) is **unsorted** and *every* index is
separate and points into the heap.

Because the row lives in the clustered index:
- Looking up a row **by primary key is very fast** — you walk one B-tree straight
  to the row data, no second jump.
- Rows with nearby primary keys are stored physically close together, which is
  good for range scans on the primary key.

### 3.2 Secondary indexes

A **secondary index** (any index that is *not* the primary key) is a separate
B-tree. But its leaf entries do **not** store a physical row pointer — they store
the **primary key value** of the row.

```
Secondary index on (name):
   [ ... "Anjali" -> PK=42 ... ]   leaf stores the PRIMARY KEY, not a row address

Lookup "Anjali":
  1) search secondary index  -> get PK=42
  2) search clustered index with PK=42 -> get the full row   (this is "back to the table")
```

So a secondary-index lookup usually needs **two B-tree searches**. This is a
trade-off of the clustered design (PostgreSQL's index points straight at the heap
location, one jump — but PostgreSQL's table is unsorted).

One small note: if all the columns you need are already *in* the secondary index,
InnoDB can skip step 2 (a "covering index").

### 3.3 Buffer pool

The **buffer pool** is InnoDB's cache of pages in RAM. Same purpose as
PostgreSQL's shared buffers: avoid hitting disk. InnoDB uses an **LRU list**
(least-recently-used) to decide which pages to keep, with a tweak that protects
recently-loaded pages from being pushed out too fast by big scans. Changed pages
are "dirty" and get flushed to disk later.

### 3.4 Updates: in-place + undo logs (vs PostgreSQL)

This is the most important difference from PostgreSQL.

- **PostgreSQL:** UPDATE creates a *new* row version and leaves the old one as
  dead (cleaned later by VACUUM). Old versions live in the table itself.
- **InnoDB:** UPDATE changes the row **in place** in the clustered index, but
  **before** changing it, it copies the *old* version into the **undo log**.

```
UPDATE row id=42 (salary 100 -> 120)

  undo log:  save old version (salary=100, ...)   <- can rebuild old value
  clustered index:  row id=42 now has salary=120  <- changed in place
```

The undo log has two jobs:
1. **Rollback** — if the transaction aborts, InnoDB uses the undo log to put the
   old value back.
2. **MVCC reads** — if another transaction has an older snapshot, InnoDB uses the
   undo log to reconstruct the **old version** of the row that that transaction is
   allowed to see. This is "Oracle-style MVCC": current data is in place, old
   versions are rebuilt from undo.

So InnoDB doesn't keep many versions inside the table like PostgreSQL — it keeps
*one* current row and rebuilds older ones from undo on demand.

### 3.5 Redo log

The **redo log** is for **durability** (same idea as PostgreSQL's WAL). Before a
committed change is guaranteed safe, InnoDB writes a **redo record** describing the
change and flushes it on commit. After a crash, InnoDB **replays the redo log** to
re-apply committed changes that hadn't been written to the data files yet.

So:
- **Redo log = how to redo committed changes after a crash** (durability).
- **Undo log = how to undo a change / see the old version** (rollback + MVCC).

That's why InnoDB needs **both**.

### 3.6 Locking and isolation

- **Row-level locking:** InnoDB locks individual **rows**, not whole tables, so
  many transactions can write to different rows at the same time. (Older MyISAM
  only had table locks — much worse for concurrency.)
- **Gap locks:** InnoDB can also lock the **gap between** index values (the empty
  space where a row *could* be inserted). This stops other transactions from
  inserting a new row into that range — it prevents **phantom rows**. This mostly
  matters in the `REPEATABLE READ` isolation level.
- **Isolation levels:** InnoDB supports all four SQL isolation levels
  (`READ UNCOMMITTED`, `READ COMMITTED`, `REPEATABLE READ`, `SERIALIZABLE`). Its
  **default is `REPEATABLE READ`**, which uses MVCC snapshots plus gap locks.
  (PostgreSQL's default is `READ COMMITTED`.)

---

## 4. Design Trade-Offs

**Advantages of InnoDB's design**
- **Primary-key lookups are very fast** because the row lives in the clustered
  index (no extra jump).
- **No table bloat from old versions** like PostgreSQL — old versions live in the
  undo log and get purged, and current rows are updated in place.
- **Row-level locking + MVCC** = good concurrency.
- Mature crash recovery via redo log.

**Limitations / costs**
- **Secondary-index lookups are slower** (two B-tree searches: secondary → PK →
  clustered index).
- **Choice of primary key matters a lot.** Because secondary indexes store the PK,
  a big primary key makes *every* secondary index bigger. A random primary key
  (like a random UUID) causes inserts all over the clustered B-tree → more page
  splits and slower inserts. A small, increasing PK (like auto-increment) is best.
- The undo log + purge system adds its own background work (a purge thread cleans
  up undo records that are no longer needed).

**vs PostgreSQL — the core trade**
| | PostgreSQL | InnoDB |
|---|---|---|
| Table storage | unsorted heap | clustered B-tree on PK |
| Update | new row version (append) | in-place + undo log |
| Old versions kept | in the table (dead tuples) | in undo log, rebuilt on demand |
| Cleanup | VACUUM | purge thread |
| Index → row | points to heap location | secondary points to PK |
| Crash recovery | WAL | redo log |

Neither is "right" — PostgreSQL's append style makes updates simple but needs
VACUUM; InnoDB's in-place style avoids table bloat but makes secondary indexes do
extra work and makes the PK choice critical.

---

## 5. Experiments / Observations

I poked at InnoDB in MySQL to confirm what I read.

**Default engine and isolation:**
```sql
SHOW TABLE STATUS;                       -- Engine column shows InnoDB
SELECT @@transaction_isolation;          -- REPEATABLE-READ by default
```

**Clustered vs secondary index lookup (EXPLAIN):**
```sql
CREATE TABLE emp(id INT PRIMARY KEY, name VARCHAR(50), dept VARCHAR(20), INDEX(name));
-- insert rows ...

EXPLAIN SELECT * FROM emp WHERE id = 42;     -- PK lookup: type=const / clustered
EXPLAIN SELECT * FROM emp WHERE name = 'Anjali'; -- secondary index, then back to clustered
```
The PK query used the clustered index directly. The `name` query used the
secondary index and (because I selected `*`) had to go back to the clustered index
for the rest of the columns — exactly the two-step lookup I described.

**Rollback shows undo working:**
```sql
START TRANSACTION;
UPDATE emp SET dept='X' WHERE id=42;
ROLLBACK;          -- value goes back to original -> undo log did this
SELECT dept FROM emp WHERE id=42;   -- unchanged
```

**Row locks let two sessions write at once (different rows):**
- Session A: `START TRANSACTION; UPDATE emp SET dept='A' WHERE id=1;` (not committed)
- Session B: `UPDATE emp SET dept='B' WHERE id=2;` → works immediately (different
  row). But `UPDATE ... WHERE id=1` in Session B would **wait** for A's row lock.
  This showed row-level locking clearly.

---

## 6. Key Learnings

- **Clustered index = the table sorted by primary key, with full rows in the leaf
  pages.** This makes PK lookups fast and is the single biggest structural
  difference from PostgreSQL.
- **InnoDB needs both undo and redo logs** because they do opposite jobs: undo =
  go *back* (rollback + rebuild old versions for MVCC), redo = re-apply committed
  changes after a crash (durability). PostgreSQL gets MVCC from in-table versions
  instead of an undo log, which is why it only needs WAL but then needs VACUUM.
- **Secondary indexes store the primary key**, so a secondary lookup is two
  searches, and the primary key should be small and increasing.
- **Row-level locking + gap locks** give good concurrency and prevent phantoms;
  default isolation is REPEATABLE READ.
- Big takeaway: PostgreSQL and InnoDB solve MVCC two genuinely different ways
  (append-new-version vs update-in-place-with-undo). Comparing them taught me that
  "supporting MVCC" doesn't fix the design — there are still real trade-offs in
  *how* you do it (table bloat vs undo overhead, one index jump vs two).
