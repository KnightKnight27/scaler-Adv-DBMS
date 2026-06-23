# MySQL / InnoDB Storage Engine

**Name:** Tirth Shah
**Roll Number:** 24BCS10347
**Course:** Advanced DBMS — System Design Discussion

---

## 1. Problem Background

MySQL was conceived in the mid-1990s as a fast, simple SQL database, and its
original default storage engine, **MyISAM**, reflected that era's priorities:
compact on-disk files, very fast read-mostly access, and full-text search. But
MyISAM made a fundamental set of compromises that became unacceptable as MySQL
moved from "the database behind a personal website" to "the database behind
large transactional web applications":

- **No transactions.** MyISAM has no `BEGIN`/`COMMIT`/`ROLLBACK`. A multi-statement
  business operation could half-complete.
- **No crash safety.** A power loss mid-write could leave indexes and data files
  inconsistent, requiring a `REPAIR TABLE` that could take hours on large tables.
- **Table-level locking only.** Every write locked the *entire table*, so a single
  slow `UPDATE` serialized all other writers. This collapses under concurrent OLTP.
- **No referential integrity** (no foreign keys).

**InnoDB** exists to solve exactly these problems. It is a general-purpose,
**transactional, ACID-compliant, crash-safe** storage engine designed for the
high-concurrency OLTP workloads typical of web applications. Its defining
features are:

- **Row-level locking** instead of table locks, so many transactions can write
  different rows concurrently.
- **Multi-Version Concurrency Control (MVCC)**, so readers never block writers and
  writers never block readers — a read sees a consistent snapshot without taking
  locks.
- **Durability and crash recovery** via **Write-Ahead Logging (WAL)** (the redo
  log) plus a **doublewrite buffer** that protects against torn pages.
- **Foreign keys** and full ACID semantics.

InnoDB became the **default storage engine in MySQL 5.5 (2010)**, displacing
MyISAM. Today it is the engine you should assume for any serious MySQL workload.
The rest of this document explains *how* it delivers these guarantees, and at
what cost — with a particular emphasis on contrasting its MVCC and indexing
design against PostgreSQL.

Throughout, assume InnoDB's defaults: **16 KB pages** and the **REPEATABLE READ**
isolation level.

---

## 2. Architecture Overview

InnoDB is split into **in-memory structures** (lost on crash, rebuilt from disk
+ logs) and **on-disk structures** (the durable source of truth).

```
┌──────────────────────────────────────────────────────────────────────────┐
│                          InnoDB IN-MEMORY STRUCTURES                       │
│                                                                            │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                          BUFFER POOL                                 │  │
│  │   Caches 16KB pages (data + index). LRU list split into:             │  │
│  │     ┌─────────────────┐   midpoint   ┌──────────────────────────┐    │  │
│  │     │  NEW / "young"  │  insertion   │   OLD / "old" sublist    │    │  │
│  │     │  (hot pages)    │ <─────────── │  (newly read pages here) │    │  │
│  │     └─────────────────┘              └──────────────────────────┘    │  │
│  │   Also holds: dirty page list (flush list), free list                │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                                                            │
│  ┌───────────────┐  ┌────────────────────┐  ┌──────────────────────────┐ │
│  │ CHANGE BUFFER │  │ ADAPTIVE HASH INDEX │  │       LOG BUFFER         │ │
│  │ (buffers      │  │ (auto hash on hot   │  │ (in-RAM redo records,    │ │
│  │  secondary    │  │  B+-tree pages →    │  │  flushed to redo log on  │ │
│  │  index writes │  │  O(1) point lookup) │  │  commit / when full)     │ │
│  │  to non-      │  └────────────────────┘  └──────────────────────────┘ │
│  │  resident     │                                                        │
│  │  pages)       │                                                        │
│  └───────────────┘                                                        │
└──────────────────────────────────────────────────────────────────────────┘
                │                                  │
                │ flush dirty pages                │ append redo records
                ▼                                  ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                           InnoDB ON-DISK STRUCTURES                        │
│                                                                            │
│  ┌────────────────────┐   ┌──────────────────────────────────────────┐   │
│  │  SYSTEM TABLESPACE │   │   FILE-PER-TABLE TABLESPACES (*.ibd)      │   │
│  │  (ibdata1):        │   │   one .ibd per table; each holds:         │   │
│  │   - data dict      │   │     ┌───────────────────────────────┐    │   │
│  │   - doublewrite*   │   │     │ CLUSTERED INDEX (= the rows,   │    │   │
│  │   - (legacy undo)  │   │     │   ordered by primary key)      │    │   │
│  └────────────────────┘   │     ├───────────────────────────────┤    │   │
│                           │     │ SECONDARY INDEXES              │    │   │
│  ┌────────────────────┐   │     │   (cols + PK value as pointer) │    │   │
│  │  REDO LOG FILES    │   │     └───────────────────────────────┘    │   │
│  │  (#ib_redoNNN /    │   └──────────────────────────────────────────┘   │
│  │   ib_logfile*)     │   ┌──────────────────────────────────────────┐   │
│  │  physical WAL,     │   │   UNDO TABLESPACES (undo_001, ...)        │   │
│  │  circular          │   │   rollback segments → old row versions    │   │
│  └────────────────────┘   │   for MVCC + rollback                     │   │
│                           └──────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────────────────┐    │
│  │ DOUBLEWRITE BUFFER (own files since 8.0.20): pages written here    │    │
│  │ first, then to final location → protects against torn/partial pages│    │
│  └──────────────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────────────┘
```

### Write path (the WAL discipline)

```
1. Modify page in BUFFER POOL  ──► page becomes "dirty"
2. Write redo record to LOG BUFFER (describes the physical change)
3. On COMMIT: flush redo from log buffer ──► REDO LOG (fsync)   ◄── durability point
                                              (WAL rule: log before data)
4. Later, asynchronously, a dirty page is flushed:
        page ──► DOUBLEWRITE BUFFER (fsync) ──► final DATA FILE (.ibd) (fsync)
   (doublewrite ensures a torn write can be recovered from the clean copy)
```

The key invariant (**Write-Ahead Logging**): the redo record for a change must be
durable on disk *before* the corresponding data page is allowed to be flushed.
This means a committed transaction is recoverable even though its data pages may
still be sitting dirty in the buffer pool at crash time.

### Read path

```
SELECT ──► is page in BUFFER POOL?
            ├─ YES (hit) ──► read row, possibly via ADAPTIVE HASH INDEX
            └─ NO  (miss)─► read 16KB page from .ibd into buffer pool
                            (inserted at OLD sublist midpoint; promoted to
                             NEW sublist only if accessed again later)
            For MVCC: if the row's version is too new for this txn's read view,
            walk DB_ROLL_PTR back into the UNDO logs to reconstruct an older version.
```

---

## 3. Internal Design

### 3.1 Clustered index & primary key storage

The single most important fact about InnoDB: **the table *is* its primary-key
B+-tree.** There is no separate heap of rows plus an index pointing at it
(as in PostgreSQL). The rows themselves live in the **leaf pages of the
primary-key index**, sorted by primary key. This is the **clustered index**.

```
                  CLUSTERED INDEX (B+-tree, keyed on PRIMARY KEY)
                            ┌───────────────┐
            internal nodes  │  [k=50][k=200]│   (keys + child page pointers)
                            └───┬───────┬───┘
                  ┌─────────────┘       └──────────────┐
                  ▼                                     ▼
          ┌────────────────┐                   ┌────────────────┐
   leaf   │ PK=10 | FULL   │ ─── linked ──────►│ PK=200| FULL   │
   pages  │ PK=20 | ROW    │     list of       │ PK=210| ROW    │
          │ PK=50 | DATA   │     leaves        │ ...   | DATA   │
          └────────────────┘                   └────────────────┘
              ▲ leaf node holds the ENTIRE row (all columns)
```

**Choice of clustering key** (in order):
1. The user-declared `PRIMARY KEY`.
2. Else the first `UNIQUE` index whose columns are all `NOT NULL`.
3. Else InnoDB synthesizes a hidden, monotonically increasing **6-byte `ROWID`**
   (`GEN_CLUST_INDEX`). This is shared across all such tables via a global
   counter — itself a reason to *always* declare your own PK.

**Page structure (16 KB by default).** Each page is self-describing:

```
┌───────────────────────────────────────────────────────────────┐
│ FIL HEADER (38B): page no, prev/next page, page type, LSN,     │
│                   space id, checksum                           │
├───────────────────────────────────────────────────────────────┤
│ PAGE HEADER / INDEX HEADER: # records, free space, level, ...  │
├───────────────────────────────────────────────────────────────┤
│ INFIMUM / SUPREMUM system records (low/high sentinels)         │
├───────────────────────────────────────────────────────────────┤
│ USER RECORDS  (rows, stored in a singly-linked list in key     │
│                order; grow downward from the top)              │
│        ▼ ▼ ▼                                                   │
│              ... free space ...                                │
│        ▲ ▲ ▲                                                   │
│ PAGE DIRECTORY (slots pointing at every ~6th record →          │
│                 enables BINARY SEARCH within the page)         │
├───────────────────────────────────────────────────────────────┤
│ FIL TRAILER (8B): old-style checksum + low 32 bits of LSN      │
│                   (must match FIL header LSN → torn-page check)│
└───────────────────────────────────────────────────────────────┘
```

**Why PK range scans are fast.** Because rows are physically stored in PK order
and leaf pages are doubly linked, a query like `WHERE id BETWEEN 100 AND 200`
seeks once to the start and then walks contiguous leaf pages sequentially — high
locality, few random I/Os, and **the rows arrive already in PK order** (no extra
indirection to fetch column values).

### 3.2 Secondary indexes

A secondary index is a *separate* B+-tree. Crucially, its leaf entries do **not**
store a physical row pointer (page+offset). They store **the indexed column(s)
plus the primary-key value** of the matching row.

```
SECONDARY INDEX on (email)            CLUSTERED INDEX (PK = id)
┌──────────────────────────┐         ┌──────────────────────────────┐
│ email='a@x' → PK id=20    │  ┌────► │ id=20 → full row             │
│ email='b@x' → PK id=10    │──┘      │ id=10 → full row             │
│ email='c@x' → PK id=50    │         │ id=50 → full row             │
└──────────────────────────┘         └──────────────────────────────┘
        step 1: search secondary       step 2: "bookmark lookup" —
        index, get PK value             search clustered index by PK
```

**Consequence 1 — the bookmark (double) lookup.** A query that filters on a
secondary index but needs columns not in that index does *two* B+-tree
traversals: one in the secondary index to get the PK, then one in the clustered
index to fetch the row. A **covering index** (one that contains every column the
query touches) avoids the second probe entirely — the data is read straight from
the secondary leaf.

**Consequence 2 — large primary keys bloat *every* secondary index.** Since the
PK value is physically copied into every leaf entry of every secondary index, a
fat PK (e.g., a 36-char UUID string, or a multi-column composite) is duplicated
across all of them, wasting disk and cache. This is a strong argument for a
small PK — typically a `BIGINT`.

### 3.3 Buffer pool & the LRU with midpoint insertion

The buffer pool caches pages in RAM. A naïve LRU would be ruined by a single
large scan (e.g., a full-table report): it would evict the genuinely hot working
set to make room for pages read once and never reused — **scan flooding**.

InnoDB defends against this with **midpoint insertion LRU**, splitting the LRU
list into two sublists:

```
   ┌─────────────────────────────┐   midpoint   ┌────────────────────────────┐
   │   NEW / "young" sublist     │  (~5/8 : 3/8)│   OLD / "old" sublist       │
   │   frequently-used pages     │ ◄──promote── │  newly read pages land HERE │
   │   (head = most recent)      │              │  (tail = first to evict)    │
   └─────────────────────────────┘              └────────────────────────────┘
```

- A newly read page is inserted at the **head of the OLD sublist**, *not* the
  head of the whole list.
- It is promoted into the NEW sublist only if it is accessed *again* after a
  short time window (`innodb_old_blocks_time`).
- A one-shot scan therefore parks its pages in the OLD sublist and they age out
  from there without disturbing the hot NEW sublist.

**Read-ahead** prefetches pages it predicts will be needed (linear read-ahead on
sequential access; random read-ahead). **Flushing** writes dirty pages back via
background threads (and through the doublewrite buffer). **Eviction** takes the
least-recently-used clean page from the OLD tail.

### 3.4 Undo logs — old versions for MVCC and rollback

Every clustered-index row carries hidden system columns:

| Hidden column | Size  | Purpose                                                       |
|---------------|-------|---------------------------------------------------------------|
| `DB_TRX_ID`   | 6 B   | transaction id that last inserted/updated this row            |
| `DB_ROLL_PTR` | 7 B   | "roll pointer" → location of the undo log record for prior ver|
| `DB_ROW_ID`   | 6 B   | row id (only present if InnoDB synthesized the clustering key)|

When a transaction updates a row, InnoDB modifies the row **in place** in the
clustered index, but first writes the *previous* version into an **undo log
record** and sets the new row's `DB_ROLL_PTR` to point at it. Undo records form
a chain — a version history:

```
  clustered-index row (current)              UNDO LOG (older versions)
  ┌───────────────────────────┐    roll_ptr  ┌───────────────────────────┐
  │ id=20, bal=300            │ ───────────► │ bal=200, trx=87, roll_ptr │ ──┐
  │ DB_TRX_ID=91              │              └───────────────────────────┘   │
  │ DB_ROLL_PTR ──────────────┘                                              │
  └───────────────────────────┘              ┌───────────────────────────┐ ◄┘
                                              │ bal=100, trx=42, roll_ptr=∅│
                                              └───────────────────────────┘
```

This is the classic **Oracle-style "undo-based" MVCC**: the main table stays
clean (one current row), and old versions live off to the side in undo. To serve
a read, a transaction consults its **read view** (the set of transactions visible
to it). If the current row's `DB_TRX_ID` is too new to be visible, InnoDB walks
the `DB_ROLL_PTR` chain backward, applying undo records until it reaches a
version that *is* visible — reconstructing the row as that transaction should see
it.

Undo serves two purposes:
1. **MVCC** — reconstructing old row versions for consistent reads.
2. **Rollback** — if a transaction aborts, its undo records are applied to
   logically reverse its changes.

Undo logs live in **rollback segments** inside **undo tablespaces**
(`undo_001`, `undo_002`, ... — separate files since MySQL 8.0). A background
**purge thread** reclaims undo records (and physically removes delete-marked
rows) once *no* active read view could still need that old version. A long-running
transaction holds back the purge horizon → **undo can grow without bound** and
the **history list length** climbs — a classic InnoDB pathology.

### 3.5 Redo logs — physical WAL, checkpoints, doublewrite

The **redo log** is InnoDB's Write-Ahead Log. It records the *physical /
physiological* changes made to pages ("on page P at offset O, write these bytes",
or "insert this record into page P") rather than logical SQL.

**The flow:**
```
change a page  ──► append redo record to LOG BUFFER (in RAM)
COMMIT         ──► flush log buffer ──► REDO LOG files on disk + fsync
                   (controlled by innodb_flush_log_at_trx_commit; =1 = full ACID)
```

The redo log files are a **fixed-size circular buffer** (`#ib_redoNNN` files in
8.0.30+, formerly `ib_logfile0/1`). Each change advances the **LSN (Log Sequence
Number)** — a monotonically increasing byte offset into the logical log stream.
Every page also stamps the LSN of its last modification in its FIL header.

**WAL rule:** a dirty data page may not be flushed to its `.ibd` file until the
redo records describing its changes are durable. Therefore the on-disk data files
can lag behind committed transactions; the redo log is what makes those commits
durable.

**Checkpointing (fuzzy checkpoint).** InnoDB periodically advances a *checkpoint
LSN*: the point up to which all changes are guaranteed flushed to data files. A
**fuzzy checkpoint** does not freeze the system — it flushes dirty pages in the
background and advances the checkpoint LSN to the oldest unflushed
modification's LSN. Recovery only needs to replay redo from the last checkpoint
LSN forward, which also bounds how far back the circular log must be retained.

**Crash recovery** is then a two-phase process:
```
1. REDO phase  (roll forward): scan redo from checkpoint LSN to end of log;
                 re-apply every change to pages whose stored LSN is older.
                 → restores ALL committed-and-logged changes, even uncommitted ones.
2. UNDO phase  (roll back): using the undo logs, reverse the effects of any
                 transactions that were NOT committed at crash time.
   → result: a state containing exactly the committed transactions.
```

**Doublewrite buffer — defending against torn pages.** A 16 KB InnoDB page is
larger than the typical 4 KB hardware sector, so a crash mid-write can leave a
**partially written ("torn") page** that redo *cannot* fix (redo assumes a
consistent base page). InnoDB therefore writes each dirty page **twice**: first
sequentially into the **doublewrite buffer**, fsync, *then* to its real location.
On recovery, if a page's checksum is bad, InnoDB restores the intact copy from
the doublewrite buffer before applying redo.

```
dirty page ──► [doublewrite buffer] (fsync) ──► [final .ibd location] (fsync)
                       │
                       └─ on crash: torn final page? → recover clean copy from here
```

### 3.6 Row-level locking: shared/exclusive, gap, next-key, intention

InnoDB locks at the **index record** level, not the row-in-a-heap level. The lock
types:

| Lock                | Symbol | Locks                          | Typical use                         |
|---------------------|--------|--------------------------------|-------------------------------------|
| Shared record (S)   | `S`    | a specific index record        | `SELECT ... LOCK IN SHARE MODE`     |
| Exclusive record (X)| `X`    | a specific index record        | `UPDATE`/`DELETE`/`FOR UPDATE`      |
| Gap lock            | gap    | the *space between* two records| prevent inserts into a range        |
| Next-key lock       | `S/X`+gap | a record **and** the gap before it | the default RR locking unit   |
| Insert intention    | II     | a gap, signalling intent to insert | many inserts coexist if disjoint|
| Intention (IS/IX)   | `IS`/`IX` | table-level, signals row-lock intent | coexistence/coarse compatibility|

**Next-key lock = record lock + gap lock on the gap preceding the record.** Under
the default **REPEATABLE READ**, locking reads and range modifications take
next-key locks. This is how InnoDB prevents **phantom rows**: by locking not just
the existing matching rows but the *gaps* around them, a concurrent transaction
cannot `INSERT` a new row that would appear in the range.

```
index values:        10        20        30        40
gaps:           (-∞,10)  (10,20)  (20,30)  (30,40)  (40,+∞)
A next-key lock on record 30 locks:   record 30   +   gap (20,30)
→ no other txn may INSERT a value in (20,30], so the range result set is stable.
```

**Intention locks** (`IX`/`IS`) are table-level and exist purely to make
table-level locks (e.g., `LOCK TABLES`, DDL) and row locks compatible to reason
about without scanning every row lock.

**Deadlock detection.** InnoDB maintains a waits-for graph and, on detecting a
cycle, **automatically rolls back the transaction that has done the least work**
(smallest set of locks/undo), returning error 1213 to the application, which
should retry.

### 3.7 Transaction processing & isolation levels

InnoDB offers all four SQL isolation levels; the engine default is **REPEATABLE
READ**.

| Level             | Dirty read | Non-repeatable read | Phantom | How InnoDB implements it                                                   |
|-------------------|:----------:|:-------------------:|:-------:|----------------------------------------------------------------------------|
| READ UNCOMMITTED  |  possible  |     possible        | possible| reads latest row, ignores read view ("dirty read")                          |
| READ COMMITTED    |     no     |     possible        | possible| a **fresh read view per statement**; gap locks largely disabled            |
| REPEATABLE READ * |     no     |        no           |   no ** | **one read view for the whole transaction** + **next-key locks**           |
| SERIALIZABLE      |     no     |        no           |   no    | RR + implicitly converts plain `SELECT` to `LOCK IN SHARE MODE`            |

`*` = InnoDB default.  `**` InnoDB's RR avoids phantoms via next-key locks, which
is stronger than the SQL standard requires of RR.

**Two flavors of read:**
- **Consistent nonlocking read** (plain `SELECT`): served from the MVCC snapshot
  via the read view + undo. Takes **no locks** — readers don't block writers.
  Under RR, the snapshot is fixed at the first such read and reused all
  transaction long; under RC, it refreshes each statement.
- **Locking read** (`SELECT ... FOR UPDATE`, `... FOR SHARE` /
  `LOCK IN SHARE MODE`, and all `UPDATE`/`DELETE`): reads the *current* committed
  data and takes `X`/`S` (next-key) locks, so it *does* interact with other
  writers.

The combination — **MVCC for snapshot reads** plus **next-key locking for
writes/locking-reads** — is what lets InnoDB deliver REPEATABLE READ *without*
phantom rows while keeping ordinary reads lock-free.

---

## 4. Design Trade-Offs

### 4.1 Why does InnoDB need BOTH undo and redo logs?

They solve *opposite, complementary* problems and cannot substitute for each
other:

| Aspect          | **Redo log**                                  | **Undo log**                                        |
|-----------------|-----------------------------------------------|-----------------------------------------------------|
| Direction       | Roll **forward**                              | Roll **backward**                                   |
| Content         | Physical *new* page changes                   | Logical *old* row versions                          |
| Primary purpose | **Durability** — replay committed changes after crash | **MVCC** snapshots + **rollback** of uncommitted txns |
| Lifetime        | until checkpoint passes the change            | until no read view needs the version (purge)        |
| Location        | redo log files (circular)                     | undo tablespaces / rollback segments                |

In one sentence: **redo answers "what did committed transactions do, so I can
re-do it after a crash?"; undo answers "what did the row look like *before*, so I
can roll back or show an older reader the old version?"** A crash recovery uses
*both*: redo rolls forward everything in the log (including uncommitted work that
was flushed), then undo rolls back whatever wasn't committed.

### 4.2 Clustered index — advantages and costs

**Advantages**
- **Fast PK range scans / ordered retrieval** — rows are physically PK-sorted
  with linked leaves; `ORDER BY pk`, range filters, and PK joins enjoy locality
  and sequential I/O.
- **PK point lookup returns the whole row in one B+-tree traversal** — no extra
  hop to a heap.
- **Natural clustering of related rows** (e.g., all order-lines for an order if
  the PK leads with `order_id`).

**Costs**
- **Secondary index indirection** — every non-covering secondary lookup pays a
  second clustered-index probe (the bookmark lookup).
- **Fat PKs bloat all secondary indexes** (the PK is copied into each).
- **Insert ordering matters enormously.** Because rows go into the leaf at their
  PK position, **random PKs (UUIDv4, hashes)** scatter inserts across the whole
  B+-tree, causing **page splits**, poor fill factor, cache misses, and write
  amplification. **Monotonically increasing PKs** (`AUTO_INCREMENT BIGINT`, or
  time-ordered UUIDv7/ULID) append to the rightmost leaf — minimal splits, dense
  pages. *Recommendation:* prefer a small, monotonic PK; if a UUID is required,
  use a time-ordered variant.

### 4.3 Why did PostgreSQL choose a different MVCC model?

Both engines do MVCC, but store old versions in fundamentally different places:

- **InnoDB (undo-based, in-place update).** Updates the row *in place* in the
  clustered index and pushes the prior version into the **undo log**. The main
  table stays compact; old versions are off to the side and reclaimed by the
  **purge thread**. *Upside:* the table doesn't bloat with dead rows; reads of the
  current version are direct. *Downside:* undo can grow under long transactions,
  reading an *old* snapshot costs extra work (walking the undo chain), and update
  has to write an undo record.

- **PostgreSQL (heap append, `xmin`/`xmax`).** An `UPDATE` writes an *entirely new
  tuple version* into the heap and marks the old one dead (via the
  `xmin`/`xmax` system columns). No undo segment exists. *Upside:* **rollback is
  almost free** (just don't make the new tuple visible), and the newest version
  is read directly. *Downside:* dead tuples accumulate in the heap → **table and
  index bloat**, requiring **`VACUUM`** (and autovacuum) to reclaim space and
  prevent transaction-id wraparound. Every index entry also points at a physical
  tuple, so updates can require updating all indexes (mitigated by **HOT
  updates** when no indexed column changes).

This is a genuine design trade-off, summarized:

| Dimension            | **InnoDB (undo, in-place)**                          | **PostgreSQL (heap append, xmin/xmax)**                  |
|----------------------|------------------------------------------------------|----------------------------------------------------------|
| Update cost          | in-place edit + write undo record                    | insert new tuple version (+ index entries unless HOT)    |
| Read of *current*    | direct (one current row in clustered index)          | direct (visible tuple in heap)                           |
| Read of *old* snapshot | extra work: walk undo chain to reconstruct          | direct: old tuple version still physically present        |
| Rollback cost        | apply undo records (work ∝ changes)                  | cheap: dead-mark the new tuple, no copy-back              |
| Space behavior       | main table compact; **undo** may grow under long txns| heap **bloats** with dead tuples until vacuumed           |
| Maintenance          | background **purge thread** (largely transparent)    | **VACUUM / autovacuum** (must be tuned; wraparound risk)  |
| Long-txn hazard      | undo grows, history list length climbs, purge stalls | vacuum can't reclaim dead tuples → bloat grows            |

Neither is universally better: InnoDB optimizes for a clean, compact main table
and cheap reads of the *current* version (great for write-then-read-current OLTP);
PostgreSQL optimizes for cheap rollback and simple, lock-free version reads at the
cost of needing vacuum.

### 4.4 Locking model trade-offs

- **Row + gap locks** dramatically reduce contention vs MyISAM table locks: many
  writers proceed concurrently as long as they touch disjoint records/gaps.
- **Gap locks can surprise developers.** Under REPEATABLE READ, a range
  `UPDATE`/`SELECT ... FOR UPDATE` locks gaps where *no rows exist yet*, blocking
  unrelated `INSERT`s into that range. This causes "why is my insert hanging?"
  confusion and can produce deadlocks between transactions inserting into nearby
  gaps. Switching to **READ COMMITTED** (which largely disables gap locks)
  reduces this — at the cost of giving up phantom protection.
- **Deadlocks are inherent** to fine-grained locking; the app must be prepared to
  catch error 1213 and retry. Consistent lock ordering and short transactions
  minimize them.

---

## 5. Experiments / Observations

> **Representative behavior (illustrative, from documented InnoDB semantics).**
> The outputs below are *not* from a live run on this machine — there is no MySQL
> instance available here. They are realistic, hand-constructed examples that
> reflect documented MySQL 8.0 behavior, for discussion purposes.

### 5.1 EXPLAIN — clustered PK lookup vs secondary-index bookmark lookup

Schema for the examples:

```sql
CREATE TABLE customers (
  id      BIGINT       NOT NULL AUTO_INCREMENT PRIMARY KEY,   -- clustered index
  email   VARCHAR(255) NOT NULL,
  city    VARCHAR(100),
  KEY idx_email (email)                                       -- secondary index
);
CREATE TABLE orders (
  id          BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
  customer_id BIGINT NOT NULL,
  amount      DECIMAL(10,2),
  KEY idx_customer (customer_id)
);
```

**(a) Primary-key point lookup** — single clustered-index traversal:

```sql
EXPLAIN SELECT * FROM customers WHERE id = 42;
```
```
+----+-------------+-----------+-------+---------------+---------+---------+-------+------+----------+-------+
| id | select_type | table     | type  | possible_keys | key     | key_len | ref   | rows | filtered | Extra |
+----+-------------+-----------+-------+---------------+---------+---------+-------+------+----------+-------+
|  1 | SIMPLE      | customers | const | PRIMARY       | PRIMARY | 8       | const |    1 |   100.00 | NULL  |
+----+-------------+-----------+-------+---------------+---------+---------+-------+------+----------+-------+
```
`type=const` + `key=PRIMARY`: the row is found directly in the clustered index;
the whole row is returned by that single traversal (no secondary hop).

**(b) Secondary-index lookup that needs a bookmark lookup:**

```sql
EXPLAIN SELECT city FROM customers WHERE email = 'a@x.com';
```
```
+----+-------------+-----------+------+---------------+-----------+---------+-------+------+----------+-------+
| id | select_type | table     | type | possible_keys | key       | key_len | ref   | rows | filtered | Extra |
+----+-------------+-----------+------+---------------+-----------+---------+-------+------+----------+-------+
|  1 | SIMPLE      | customers | ref  | idx_email     | idx_email | 1022    | const |    1 |   100.00 | NULL  |
+----+-------------+-----------+------+---------------+-----------+---------+-------+------+----------+-------+
```
`idx_email` is used to find the PK, but `city` is **not** in that index, so InnoDB
must do a **bookmark lookup** into the clustered index by PK to fetch `city`.
(No `Using index` in Extra → not covering.)

**(c) Same shape, but now covered** — add `KEY idx_email_city (email, city)`:

```
| ... | type | key            | ... | Extra       |
| ... | ref  | idx_email_city | ... | Using index |
```
`Extra = Using index` ⇒ **covering index**: `city` is read straight from the
secondary leaf, so the bookmark lookup into the clustered index is skipped.

**(d) Join, EXPLAIN ANALYZE (MySQL 8 tree format)** — note the explicit
"Single-row index lookup on ... using PRIMARY" inner step, which *is* the
clustered-index probe driven by the secondary `idx_customer`:

```sql
EXPLAIN ANALYZE
SELECT o.id, c.email
FROM orders o JOIN customers c ON c.id = o.customer_id
WHERE o.customer_id = 42;
```
```
-> Nested loop inner join  (cost=2.10 rows=3) (actual time=0.04..0.07 rows=3 loops=1)
    -> Index lookup on o using idx_customer (customer_id=42)
         (cost=0.85 rows=3) (actual time=0.02..0.03 rows=3 loops=1)
    -> Single-row index lookup on c using PRIMARY (id=o.customer_id)
         (cost=0.30 rows=1) (actual time=0.01..0.01 rows=1 loops=3)
```
Reading it: the secondary index `idx_customer` finds matching orders; for each,
a **single-row clustered-index lookup** (`using PRIMARY`) fetches the customer —
exactly the secondary→clustered indirection described in §3.2.

### 5.2 SHOW ENGINE INNODB STATUS — annotated excerpts

```sql
SHOW ENGINE INNODB STATUS\G
```

**LOG section** — durability/checkpoint state:
```
---
LOG
---
Log sequence number          1357924680      <- current LSN (bytes of redo written)
Log buffer assigned up to    1357924680
Log flushed up to            1357924680      <- redo durable to disk up to here
Pages flushed up to          1357901234      <- dirty pages flushed to .ibd up to here
Last checkpoint at           1357890011      <- recovery would start replaying here
```
The gap between **Log sequence number** and **Last checkpoint at** is the redo
that crash recovery would have to replay; a large, ever-growing gap indicates
checkpointing/flushing is falling behind write load.

**BUFFER POOL AND MEMORY** — cache effectiveness:
```
----------------------
BUFFER POOL AND MEMORY
----------------------
Total large memory allocated 137428992
Buffer pool size             8192          <- pages (×16KB ≈ 128MB)
Free buffers                 512
Database pages               7400
Old database pages           2700          <- the OLD (midpoint) sublist
Modified db pages            128           <- dirty pages awaiting flush
Pages made young 18342, not young 240113   <- midpoint-LRU promotions vs scan pages parked in OLD
Buffer pool hit rate 998 / 1000            <- 99.8% hit rate (very healthy)
```
`Old database pages` and `Pages made young / not young` directly expose the
**midpoint-insertion LRU** of §3.3 — most scanned-once pages ("not young") never
get promoted, protecting the hot set.

**TRANSACTIONS** — MVCC purge horizon + a lock wait:
```
------------
TRANSACTIONS
------------
Trx id counter 91022
Purge done for trx's n:o < 90990 undo n:o < 0    <- purge horizon
History list length 47                            <- undo versions awaiting purge

---TRANSACTION 91020, ACTIVE 12 sec starting index read
mysql tables in use 1, locked 1
LOCK WAIT 2 lock struct(s), heap size 1136, 1 row lock(s)
... waiting for lock ...
---TRANSACTION 91019, ACTIVE 14 sec
3 lock struct(s), heap size 1136, 2 row lock(s), undo log entries 1
```
A spiking **History list length** is the canonical signal of a long-running
transaction holding back **purge** (§3.4) → undo growth. The `LOCK WAIT` line is
one transaction blocked on another's row lock.

### 5.3 Gap / next-key lock demonstration (REPEATABLE READ)

Table: `accounts(id INT PRIMARY KEY, balance INT)` with existing rows
`id ∈ {10, 20, 30}`. Both sessions use the default **REPEATABLE READ**.

```
 time │ Session A                                  │ Session B
──────┼────────────────────────────────────────────┼──────────────────────────────────
  t1  │ BEGIN;                                      │
  t2  │ UPDATE accounts SET balance = balance + 1   │
      │   WHERE id BETWEEN 10 AND 30;               │
      │  -- X next-key locks on records 10,20,30    │
      │  -- AND the gaps around them, incl. (20,30) │
      │  -- and (10,20). Statement returns.         │
  t3  │                                             │ BEGIN;
  t4  │                                             │ INSERT INTO accounts VALUES (25,0);
      │                                             │  -- wants insert-intention lock in
      │                                             │  -- gap (20,30) → CONFLICTS with A's
      │                                             │  -- gap lock → **B BLOCKS / waits**
  t5  │ COMMIT;   -- releases all locks             │
  t6  │                                             │  -- B's INSERT now proceeds
      │                                             │ COMMIT;
```

**Step-by-step explanation.**
1. At `t2`, A's range `UPDATE` takes **next-key locks**: exclusive record locks on
   `10, 20, 30` *plus* gap locks on the gaps between/around them, including the gap
   `(20,30)`. This is the phantom-prevention mechanism of §3.6 — A's range result
   set is now frozen.
2. At `t4`, B tries to `INSERT id=25`, which falls in the gap `(20,30)`. Inserts
   acquire an **insert-intention lock** in the target gap, which is *incompatible*
   with A's gap lock there.
3. B therefore **blocks** even though row `25` does not yet exist and A never
   touched it — the lock is on the *gap*, not a row. This is the surprising-but-
   correct gap-lock behavior of §4.4: it guarantees that if A re-ran its range
   query it would see no new "phantom" `25`.
4. When A commits at `t5`, its gap locks release and B's insert proceeds.

Under **READ COMMITTED**, gap locks are largely disabled, so B's insert would
*not* block here — at the price of losing RR's phantom protection.

---

## 6. Key Learnings

1. **The table is the primary-key B+-tree.** InnoDB has no separate heap: rows
   live in the clustered-index leaves, sorted by PK. This makes PK range scans and
   point lookups fast but makes the choice of PK a first-class performance
   decision.

2. **Pick a small, monotonic primary key.** Random PKs (UUIDv4) cause page splits
   and write amplification; the PK is also copied into *every* secondary index, so
   a fat PK bloats them all. Prefer `BIGINT AUTO_INCREMENT` (or time-ordered
   UUIDv7/ULID).

3. **Secondary indexes point to the PK, not to a physical row.** Non-covering
   secondary lookups pay a second "bookmark" probe into the clustered index;
   covering indexes (`Using index`) eliminate it — a key tuning lever.

4. **Undo and redo are complementary, not redundant.** Redo = roll *forward* for
   **durability/crash recovery** of committed changes; undo = roll *backward* for
   **rollback** and to reconstruct old versions for **MVCC**. Recovery uses both:
   redo to roll forward, then undo to roll back uncommitted work.

5. **InnoDB's MVCC is undo-based with in-place updates**, keeping the main table
   compact (purged in the background) — versus PostgreSQL's heap-append model that
   trades cheap rollback for table bloat and `VACUUM`. Each picks a different point
   on the update-cost / read-cost / rollback-cost / maintenance trade-off curve.

6. **WAL + doublewrite + fuzzy checkpoints** together give durability without
   forcing data pages to disk on every commit: redo guarantees recoverability, the
   doublewrite buffer guards against torn pages, and checkpoints bound recovery
   time and log retention.

7. **REPEATABLE READ + next-key locks prevent phantoms** while plain reads stay
   lock-free via MVCC read views. The cost is gap locks that can block inserts into
   ranges and contribute to deadlocks — understand this before reaching for RR or
   dropping to READ COMMITTED.

8. **Watch the operational signals:** buffer-pool hit rate and "pages made
   young/not young" (LRU health), the LSN-to-checkpoint gap (flush keeping up), and
   **History list length** (long transactions starving purge → undo growth).

---

## References

1. **MySQL 8.0 Reference Manual — InnoDB Storage Engine.**
   - *InnoDB Architecture* and *In-Memory Structures* (Buffer Pool, Change Buffer,
     Adaptive Hash Index, Log Buffer).
     https://dev.mysql.com/doc/refman/8.0/en/innodb-architecture.html
   - *InnoDB On-Disk Structures* (Clustered/Secondary Indexes, System Tablespace,
     File-Per-Table, Doublewrite Buffer, Redo Log, Undo Logs, Undo Tablespaces).
     https://dev.mysql.com/doc/refman/8.0/en/innodb-on-disk-structures.html
   - *InnoDB Locking and Transaction Model* (Shared/Exclusive, Gap, Next-Key,
     Intention locks; Consistent Nonlocking Reads; Locking Reads; Isolation Levels;
     Deadlocks).
     https://dev.mysql.com/doc/refman/8.0/en/innodb-locking-transaction-model.html
   - *The Clustered Index and Secondary Indexes* / *Physical Structure of an InnoDB
     Index*.
     https://dev.mysql.com/doc/refman/8.0/en/innodb-index-types.html
   - *InnoDB Buffer Pool* and *The LRU Algorithm (Midpoint Insertion)*.
     https://dev.mysql.com/doc/refman/8.0/en/innodb-buffer-pool.html
   - *InnoDB Recovery* / *Redo Log* / *Checkpoints*.
     https://dev.mysql.com/doc/refman/8.0/en/innodb-redo-log.html
   - *EXPLAIN Output Format* and *EXPLAIN ANALYZE*.
     https://dev.mysql.com/doc/refman/8.0/en/explain-output.html

2. **Jeremy Cole, "InnoDB internals" blog series** — deep dives on the physical
   page layout, B+-tree structure, record formats, and the index/space file
   organization (`blog.jcole.us`), e.g. *"The physical structure of InnoDB index
   pages"* and *"The physical structure of records in InnoDB."*
   https://blog.jcole.us/innodb/

3. Baron Schwartz, Peter Zaitsev, Vadim Tkachenko, *High Performance MySQL*
   (O'Reilly) — chapters on schema/index design (clustered index implications,
   choosing a primary key) and InnoDB transaction/locking behavior.
