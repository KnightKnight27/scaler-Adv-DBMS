# MySQL / InnoDB Storage Engine

> Advanced DBMS · System Design Discussion · Topic 3
>
> How InnoDB lays data out on disk, handles concurrent transactions, and recovers
> after a crash. I keep PostgreSQL next to it the whole way, because the two engines
> made opposite choices and the contrast is where most of the reasoning lives.

---

## 1. Problem Background

Any storage engine has to solve three problems at the same time, and the three pull against
each other. Rows need to live on disk in a way that supports fast lookups. Many transactions
need to run at once without seeing each other's half-finished work. And the database has to
survive a crash without losing committed data or keeping uncommitted data. Durability wants
every change on disk immediately; throughput wants writes batched and deferred. A reader wants
a stable view of the data; a writer wants to change it underneath that reader. An engine is one
particular way of settling those fights. InnoDB's way is the subject of this document.

InnoDB has been MySQL's default engine since 5.5, when it replaced MyISAM. MyISAM was quick on
read-heavy workloads but had two problems that ruled it out for anything serious. It locked at
the table level, so a single write froze the whole table. And it kept no transaction log, so a
crash partway through a write could leave a table or its indexes corrupted for good. InnoDB was
built to fix exactly those two things: real ACID transactions, row-level locking, and crash
recovery, without being so slow it couldn't serve as the default.

What makes it worth studying is *how* it pulls that off. Three decisions shape almost everything
else about it:

1. The table is stored as a clustered B+tree on the primary key. There is no separate "heap".
   The index *is* the table.
2. MVCC is done in the "Oracle style": the table always holds the current version of a row, and
   older versions get rebuilt on demand from the **undo logs** when a long-running reader needs
   them.
3. Recovery is split across two logs. A redo log rolls committed work forward; undo logs roll
   uncommitted work back.

PostgreSQL answers the same three problems almost the opposite way: an unordered heap with
separate index files, every row version kept inline in the heap, and a single WAL with a
background `VACUUM` to clean up dead rows. Putting the two side by side is the quickest way to
see why each one behaves the way it does, so PostgreSQL runs as a comparison throughout and gets
its own section at §4.

---

## 2. Architecture Overview

At the coarsest level InnoDB is an **in-memory layer** and an **on-disk layer**, with background
threads moving data and log records between them.

```
                          ┌──────────────────────────────────────────────┐
                          │                 IN MEMORY                      │
   SQL layer  ───────►    │  ┌────────────────┐   ┌────────────────────┐  │
   (parser,              │   │  Buffer Pool   │   │  Log Buffer        │  │
    optimizer,           │   │  16KB pages    │   │  (pending redo)    │  │
    handler API)         │   │  LRU young/old │   └─────────┬──────────┘  │
                          │  │  change buffer │             │             │
                          │  │  adaptive hash │             │             │
                          │  └───────┬────────┘             │             │
                          └──────────┼──────────────────────┼─────────────┘
                                     │ flush dirty pages     │ flush redo
                          ┌──────────┼──────────────────────┼─────────────┐
                          │          ▼          ON DISK      ▼             │
                          │  ┌─────────────────┐   ┌────────────────────┐ │
                          │  │ Tablespaces     │   │ Redo log           │ │
                          │  │  .ibd files:    │   │ (ib_logfile /      │ │
                          │  │  clustered idx  │   │  #innodb_redo)     │ │
                          │  │  + secondary    │   ├────────────────────┤ │
                          │  │  idx B+trees    │   │ Undo logs          │ │
                          │  │                 │   │ (rollback segments │ │
                          │  └─────────────────┘   │  in system/undo TS)│ │
                          │                        └────────────────────┘ │
                          └──────────────────────────────────────────────┘
```

**Main components**

- **Clustered index** — the primary-key B+tree that physically *is* the table. Leaf pages hold
  full rows in PK order. (§3.1)
- **Secondary indexes** — separate B+trees mapping indexed columns to the **primary key value**
  of the matching row, not to a physical pointer. (§3.2)
- **Buffer pool** — the in-memory cache of 16KB pages; almost every read and write passes
  through it. (§3.3)
- **Undo logs** — per-modification "before images" used for rollback and to reconstruct old row
  versions for MVCC. (§3.4)
- **Redo log** — a write-ahead log of *physical* page changes that guarantees durability and
  drives crash recovery. (§3.5)
- **Locking subsystem** — record, gap, and next-key locks that, with MVCC snapshots, implement
  the isolation levels. (§3.6)

**Data flow**

On a *read*, the optimizer asks the InnoDB handler for rows. InnoDB walks the relevant B+tree,
faulting pages into the buffer pool on a miss. Under MVCC it may follow a row's undo chain to
build the version that the reading transaction's snapshot is allowed to see.

On a *write*, the target page is loaded into the buffer pool and changed in place. A redo record
describing the physical change goes to the log buffer, and an undo record holding the row's prior
contents goes to a rollback segment. The page is now dirty and gets flushed later.

On *commit*, the redo log is flushed to disk (how strictly depends on
`innodb_flush_log_at_trx_commit`). That flush is the moment the transaction becomes durable, even
though the dirty data pages may still be sitting in memory. Log first, data later.

---

## 3. Internal Design

### 3.1 Clustered index and primary-key storage

InnoDB doesn't keep rows in an unordered heap with indexes bolted on the side. The whole table
is one B+tree clustered on the primary key. Internal nodes hold key plus child-pointer entries
for navigation. The leaf nodes hold the complete rows, in primary-key order. Once a search
reaches the right leaf, the row is right there. The leaf entry is the row, including all its
columns and the hidden system columns `DB_TRX_ID` and `DB_ROLL_PTR`.

So every InnoDB table has a clustered index whether you asked for one or not. InnoDB picks it
like this:

1. Use the declared `PRIMARY KEY`.
2. If there isn't one, use the first `UNIQUE` index whose columns are all `NOT NULL`.
3. If there's neither, InnoDB makes a hidden, monotonically increasing 6-byte row id
   (`DB_ROW_ID`) and clusters on that.

Falling back to the hidden row id is usually a bad outcome. You give up clustering you could have
controlled, and the global row-id counter turns into a contention point under heavy inserts.

Because the table is physically ordered by the PK, the insertion order decides whether new rows
land at the end of the tree or in the middle. That matters more than it sounds:

- A **monotonically increasing** PK (`AUTO_INCREMENT`, say) sends every insert to the right-most
  leaf page. Pages fill cleanly, split at the end, stay dense, and reads come out roughly
  sequential.
- A **random** PK (a v4 `UUID` stored as a string, or any unordered binary) scatters inserts all
  over the key space. A new row keeps landing in the middle of a full leaf page, which forces a
  page split: half the rows move to a fresh page. You pay for that three ways. Extra I/O and CPU
  on every insert. Fragmentation, since pages get left about half full, so the table eats more
  disk and the buffer pool caches fewer useful rows. And cache thrashing, because the hot insert
  point is everywhere instead of one place. This is probably the most common performance problem
  people inflict on themselves with InnoDB.

The usual advice, keep the primary key small and monotonic, comes straight out of this. If you
genuinely need a UUID, a time-ordered one (UUIDv7 or ULID) avoids most of the damage because it
inserts in roughly increasing order.

```
                Clustered index (table IS this tree), keyed on PK = id
                          ┌───────────────────────────┐
            internal      │   [ 50 | 100 | 150 ]       │   ← navigation only
            node          └───┬──────┬───────┬─────────┘
                              │      │       │
              ┌───────────────┘      │       └───────────────┐
              ▼                      ▼                        ▼
   ┌────────────────────┐ ┌────────────────────┐  ┌────────────────────┐
   │ LEAF (rows 1–49)   │ │ LEAF (rows 50–99)  │  │ LEAF (rows 100–149)│
   │ id=1 | name | trx  │ │ id=50| name | trx  │  │ id=100|name | trx  │
   │ id=2 | name | trx  │ │ id=51| name | trx  │  │ id=101|name | trx  │
   │ ...  full rows ... │ │ ...  full rows ... │  │ ...  full rows ... │
   └─────────┬──────────┘ └─────────┬──────────┘  └─────────┬──────────┘
             └──────────────────────┴───────── doubly-linked leaf chain ──►
       Leaf pages are linked, so a range scan on PK is a sequential walk.
```

### 3.2 Secondary indexes

A secondary index is a B+tree too, but its leaf entries don't point at a physical row location.
Each leaf entry stores `(indexed column values → primary key value)`. So fetching a full row
through a secondary index takes a double lookup:

```
   SELECT * FROM users WHERE email = 'x@y.com';

   ┌──────────────────────────┐         ┌──────────────────────────────┐
   │ Secondary index (email)  │         │ Clustered index (PK = id)     │
   │  'a@..'  → id 17         │         │  ... id 42 → full row ...     │
   │  'x@y'   → id 42  ───────┼────────►│  leaf entry = the actual row  │
   │  'z@..'  → id 8          │  step 2 │                               │
   └──────────────────────────┘ lookup  └──────────────────────────────┘
        step 1: search by email,             walk clustered tree by id=42
        get PK value (42)                     to retrieve the row body
```

Step 1 walks the secondary tree to get the PK. Step 2 walks the clustered tree to get the row.
Two traversals for one row. Two consequences fall out of this.

First, **covering indexes**. If a query only needs columns the secondary index already holds (the
indexed columns, plus the PK, which is always in there), step 2 disappears and the query is
answered from the index alone. `EXPLAIN` shows this as `Using index`. Adding a rarely-needed
column to an index just to make a hot query covering is a standard trick, and it works precisely
because of how secondary indexes are built.

Second, a **fat primary key bloats every secondary index**. Every secondary leaf entry carries
the full PK value, so a `CHAR(36)` UUID PK tacks ~36 bytes onto every entry of every secondary
index, against ~4–8 bytes for an integer. With a few secondary indexes over millions of rows
that quietly turns into gigabytes of extra index, which means more disk, less of the buffer pool
spent on useful data, and slower scans. It's a second, independent reason to keep the PK small,
and you won't notice it until you understand the storage layout.

PostgreSQL is different here. Its index leaves store the physical tuple id (`TID`, a page plus
offset), so a secondary lookup is index → heap, one dereference into a fixed location, and the PK
width costs nothing in the secondary indexes. The trade-off is in §4.

### 3.3 Buffer pool

The buffer pool is InnoDB's in-memory cache of 16KB pages, and it's usually the biggest memory
consumer on the box (commonly 50–75% of RAM on a dedicated server). Every page read and write
goes through it; disk only gets touched on a miss or a flush.

**Midpoint-insertion LRU.** A plain LRU list would get wrecked by one big table scan or a backup.
Reading a huge table once would push the genuinely hot working set out in favor of pages nobody
will touch again. InnoDB guards against that by cutting the LRU list into a **young (new)**
sublist and an **old** sublist, joined at a midpoint (the old sublist is about 3/8 of the list by
default). A page that's just been read goes in at the head of the *old* sublist, not the head of
the whole list. It only gets promoted into the young sublist if it's read *again* after a short
time window (`innodb_old_blocks_time`). A one-shot scan parades its pages through the old sublist
and back out without ever displacing the hot pages. That's the scan-resistance built into the
algorithm.

**Dirty pages and flushing.** A modified page is dirty: its in-memory copy is ahead of disk.
Write-ahead logging means a commit only needs the redo on disk, so dirty data pages get flushed
lazily by background page-cleaner threads. That batches the writes, smooths out the I/O, and
saves work on a page that gets modified over and over before it's written once.

**Change buffer.** When you insert, update, or delete a row, its secondary indexes have to be
updated too. But the secondary-index leaf page might not be in memory, and reading it back just
to make one small change is wasteful for a non-unique index. So InnoDB records the pending change
in the **change buffer** (part of the buffer pool, persisted in the system tablespace) and merges
it in later, when the page gets read for some other reason or by a background merge. Random
secondary-index write I/O becomes deferred, batched I/O, which helps a lot on write-heavy tables
with many secondary indexes. It can't be used for unique indexes, since uniqueness has to be
checked right away.

**Adaptive hash index.** If InnoDB sees certain index pages searched over and over with the same
prefix, it builds an in-memory hash index over them on its own, collapsing a multi-level B+tree
descent into a single hash probe for those hot keys. It tunes itself and you don't configure it.

**Versus PostgreSQL.** PostgreSQL's equivalent is `shared_buffers`, and it's usually sized
*smaller* (often ~25% of RAM), because PostgreSQL leans on the OS page cache on purpose. A miss in
`shared_buffers` can still be served from the kernel cache. Its eviction is a clock-sweep
(second-chance) scan with a per-buffer usage counter, not a young/old LRU. Clock-sweep is cheaper
to maintain, since it doesn't relink a list on every hit, but it doesn't have InnoDB's explicit
scan-resistance. PostgreSQL also has nothing like the change buffer or the adaptive hash index,
which fits its different storage model where the visibility map and the OS cache carry some of
the same load.

### 3.4 Undo logs and MVCC

Every row in the clustered index carries two hidden system columns: `DB_TRX_ID`, the id of the
transaction that last touched the row, and `DB_ROLL_PTR`, a roll pointer to an undo log record.
When a transaction changes a row, InnoDB updates the row in place and writes the row's *previous*
contents into an undo log record, pointing the new row's `DB_ROLL_PTR` at it. Repeated changes
build a backward-linked version chain through the undo logs.

Undo logs do two jobs:

1. **Rollback.** If the transaction aborts, errors out, or the server crashes before commit,
   InnoDB replays the undo records to put the rows back the way they were.
2. **Consistent reads (MVCC).** When a transaction opens a *read view*, it records which
   transactions were committed at that moment. Later, if it reads a row whose `DB_TRX_ID` isn't
   visible to that snapshot (the row was changed by a transaction that committed after the
   snapshot was taken), InnoDB walks the `DB_ROLL_PTR` chain backward, applying older versions
   until it reaches the one the snapshot is allowed to see. The reader gets a consistent
   point-in-time image and takes no locks doing it. Readers don't block writers and writers don't
   block readers.

This is the "Oracle-style" MVCC model. The live table always holds the latest version of each
row, updated in place, and old versions live somewhere else, in the undo logs, materialized only
when an older reader asks for one. The table itself doesn't fill up with dead versions. The undo
logs grow instead, and a background **purge thread** throws away undo records once no active read
view could still need them.

This is the sharpest split between InnoDB and PostgreSQL. PostgreSQL keeps *all* versions of a
row inline in the heap, each tagged with `xmin` (the transaction that created it) and `xmax` (the
one that deleted or superseded it). An UPDATE in PostgreSQL doesn't change anything in place; it
writes a brand-new tuple and marks the old one expired, which is effectively DELETE + INSERT. A
reader decides visibility by comparing `xmin`/`xmax` against its snapshot, with no undo chain to
chase. The price is **table bloat**: dead tuples pile up in the heap until `VACUUM` clears them.
The trade-offs are in §4.

### 3.5 Redo logs

The redo log (`ib_logfile0/1` historically, the `#innodb_redo` directory in recent versions) is a
physical write-ahead log. It records low-level page changes: "on page P at offset O, these bytes
became these bytes." It's a fixed-size set of files used as a circular buffer.

Its job is durability and crash recovery. The rule is write-ahead: before a dirty data page is
allowed to be flushed to its tablespace, the redo records describing its changes must already be
on disk. On `COMMIT`, the transaction's redo is flushed and `fsync`'d to the log files. After
that the transaction is durable. Even if the server dies before the data pages are written,
recovery can roll the committed changes forward by replaying redo from the last checkpoint.

`innodb_flush_log_at_trx_commit` sets how strict that is, and it's the main durability-vs-throughput
knob:

| Value | Behaviour on COMMIT | Guarantee |
|-------|--------------------|-----------|
| `1` (default) | Write **and** `fsync` redo every commit | Fully ACID; no committed transaction lost on crash |
| `2` | Write to OS cache every commit, `fsync` ~once/sec | Survives a *process* crash; an *OS/power* crash can lose ~1s |
| `0` | Write + `fsync` only ~once/sec | Fastest; up to ~1s of commits lost on any crash |

**Why both logs exist.** Redo and undo answer two different questions during recovery, and neither
can stand in for the other. Redo answers "which committed changes might not have reached the data
files?" and rolls them forward. It's needed *because* InnoDB defers flushing dirty data pages for
performance; without redo, deferring would be unsafe. Undo answers "which changes already on disk
belong to transactions that never committed?" and rolls them back. It's needed *because* pages are
modified in place and flushed lazily, so a dirty page from an uncommitted transaction can easily
reach disk before a crash, and something has to clean it up.

Recovery is therefore two passes. Replay redo forward to get to a consistent post-crash state,
re-applying everything that was logged whether it committed or not. Then use undo to roll back the
transactions that hadn't committed. Redo gives durability of the committed; undo gives atomicity
of the aborted. PostgreSQL only needs WAL for redo, because it never updates a row in place and the
old versions are already in the heap. Its "undo" is implicit in the tuple visibility rules, which
is a direct result of the heap-versioning model.

### 3.6 Row-level locking, gap locks, and isolation

For reads, InnoDB mostly skips locks and uses MVCC snapshots. For writes, and for locking reads
like `SELECT ... FOR UPDATE`, it takes fine-grained locks on **index records**:

- **Record locks** — a shared (`S`) or exclusive (`X`) lock on a single index record.
- **Gap locks** — a lock on the *gap between* two index records (or before the first, or after the
  last). A gap lock blocks **inserts** into that range but locks no existing row. Its only purpose
  is to stop phantoms.
- **Next-key locks** — the default unit at `REPEATABLE READ`: a record lock on a row plus a gap
  lock on the gap *before* it. By locking both the rows a range scan touches and the gaps between
  them, a range query stops other transactions from inserting rows that would show up if the query
  ran again.

```
   index values:      …  10        20            30  …
   gap locks:           [<-gap->][<-gap->][<-gap->]
   next-key on 20  =  (gap before 20) + (record 20)
   → an INSERT of 15 or 25 into a locked gap must wait
```

**Isolation levels in InnoDB:**

- **READ COMMITTED** takes a fresh read view per statement, so it sees other transactions'
  committed changes between statements (non-repeatable reads are possible). Gap locking is mostly
  turned off, so only existing rows get locked and phantoms can happen. Less locking means more
  concurrency and fewer deadlocks, with weaker guarantees.
- **REPEATABLE READ** (the InnoDB default) takes one read view at the first read and reuses it for
  the whole transaction, so plain reads are consistent and repeatable. The interesting part is
  that InnoDB pairs this with next-key locking on locking reads and writes, which blocks phantom
  inserts into ranges the transaction has scanned. The SQL standard allows phantoms at REPEATABLE
  READ, but InnoDB's next-key locks get rid of them, so its RR is actually stronger than the
  standard asks for.
- **SERIALIZABLE** is like RR except InnoDB quietly turns plain `SELECT`s into
  `SELECT ... LOCK IN SHARE MODE`, so even pure reads take shared next-key locks. Most isolation,
  most locking, most deadlock risk.

PostgreSQL gets to similar guarantees by a different route. Its REPEATABLE READ is pure snapshot
isolation with no gap locks, because it never has to stop inserts into a range it "owns". Its
SERIALIZABLE is real Serializable Snapshot Isolation (SSI), which watches for dangerous read/write
dependency cycles and aborts a transaction at commit time instead of locking up front. InnoDB
blocks now (pessimistic); PostgreSQL SSI aborts later (optimistic).

---

## 4. Design Trade-Offs

The whole point of this topic is the head-to-head with PostgreSQL. Both engines are internally
consistent. They just bet opposite ways.

### Core comparison

| Aspect | PostgreSQL | InnoDB |
|--------|-----------|--------|
| **Table storage** | Unordered **heap** + separate index files | Table **is** the clustered PK B+tree; rows live in leaf pages |
| **Secondary index leaf** | Points to physical tuple id (`TID`) → 1 heap fetch | Stores **PK value** → second B+tree lookup (double dereference) |
| **UPDATE** | Append a **new tuple** (DELETE+INSERT), old tuple stays in heap | **In-place** update; previous version copied to **undo log** |
| **MVCC versions** | **All** versions inline in heap, tagged `xmin`/`xmax` | **Latest** in clustered index; old versions via `DB_ROLL_PTR` → undo chain |
| **Visibility check** | Compare `xmin`/`xmax` to snapshot — no chain walk | Walk undo chain back to the version the read view may see |
| **Dead-version cleanup** | `VACUUM` reclaims dead heap tuples (+ autovacuum) | **Purge thread** discards undo no reader still needs |
| **Recovery logs** | Single **WAL** (redo only) | **Redo** (roll forward) **+ undo** (roll back) |
| **Phantom prevention (RR)** | Snapshot isolation; no gap locks | **Next-key (gap) locks** |
| **Serializable** | SSI (optimistic, abort on conflict) | Locking-based (pessimistic) |

### What each bet buys, and what it costs

**InnoDB's clustered-index bet wins on PK access and storage density.** A primary-key lookup or a
PK range scan reads the rows straight out of the index leaves. There's no separate heap fetch, and
a PK range is a sequential leaf walk. Storage stays compact because there's one copy of the data,
which is the index, and in-place updates plus the purge thread keep the table from bloating with
dead versions. The cost shows up in three places. Every secondary-index lookup pays the double
dereference. A large PK inflates every secondary index. And updates, especially random-key inserts,
can trigger page splits and fragmentation in the clustered tree, because moving or inserting a row
means reorganizing physically ordered data.

**PostgreSQL's heap bet wins on writes and secondary indexes.** Since the heap is unordered, an
insert just appends to a page that has room. There's no clustered tree to split, so insert cost
barely cares about key ordering, and a random UUID PK hurts far less than it does in InnoDB. Its
secondary indexes are cheap and uniform: each one points straight at a `TID`, one fetch, and PK
width doesn't affect index size. The cost is that keeping every version inline lets dead tuples
build up as bloat until `VACUUM` runs. An unvacuumed table can grow well past its live size, slow
down scans, and historically risk transaction-id wraparound. And because a row's physical location
can change on UPDATE, every secondary index may need updating; the `HOT` (heap-only tuple)
optimization helps, but only when the new tuple fits on the same page and no indexed column
changed.

**Why PostgreSQL chose heap + inline versioning on purpose.** It's a deliberate call, not an
accident. Keeping all versions in the heap means there's no undo segment, so no rollback-segment
contention, no "snapshot too old" from undo getting purged out from under a long reader (a familiar
Oracle/InnoDB headache), and a simple, nearly lock-free read path. Visibility is local arithmetic
on `xmin`/`xmax` against the snapshot, with no chain to follow. Rollback is close to free, since
the new tuple just never becomes visible, which makes aborting a huge transaction cheap. The cost,
bloat and `VACUUM`, gets pushed onto a background process. PostgreSQL traded a maintenance task for
a simpler, more concurrent read-and-abort path.

**Why InnoDB chose clustered storage + undo MVCC.** InnoDB optimized for the common OLTP pattern,
point and range lookups by primary key, and for compact storage that doesn't bloat. Clustering on
the PK makes those lookups fast and keeps the data dense, and undo-based MVCC keeps the live table
free of dead versions, so there's nothing like `VACUUM` sweeping the main table. It bet that
PK-centric access and in-place compactness were worth the secondary-index double lookup and the
page-split sensitivity, and that a purge thread over the smaller, sequential undo logs is cheaper
than vacuuming the whole table.

### The doc's posed questions

- **Why does InnoDB need both undo and redo?** They aren't redundant. They roll the database in
  opposite directions for opposite reasons. Redo exists because InnoDB defers flushing dirty data
  pages for performance, so recovery has to roll committed changes forward to make deferral safe.
  Undo exists because pages are modified in place and can be flushed before commit, so recovery has
  to roll uncommitted changes back, and it doubles as the source of old row versions for MVCC. Redo
  is durability of the committed; undo is atomicity of the aborted (and MVCC). (See §3.5.)
- **What advantages do clustered indexes provide?** Direct row access on a PK lookup with no heap
  fetch, fast sequential PK range scans, one compact copy of the data, and automatic physical
  clustering of related rows, since rows with adjacent PKs sit on the same page. (See §3.1.)
- **Why did PostgreSQL choose a different MVCC model?** To dodge undo-segment contention and
  "snapshot too old", to get a trivial lock-free read-visibility check and near-free rollback, at
  the accepted cost of heap bloat that `VACUUM` cleans up asynchronously. (See §3.4 and above.)

---

## 5. Experiments / Observations

These are representative observations from poking at a local InnoDB instance. The numbers show the
*direction and rough size* of each effect, not careful benchmarks.

### 5.1 Sequential `AUTO_INCREMENT` vs random `UUID` primary key on insert

I loaded two structurally identical tables with about 1,000,000 rows each, one keyed on
`BIGINT AUTO_INCREMENT`, the other on a random `CHAR(36)` UUID:

```sql
CREATE TABLE t_seq  (id BIGINT AUTO_INCREMENT PRIMARY KEY, payload CHAR(200));
CREATE TABLE t_uuid (id CHAR(36) PRIMARY KEY,               payload CHAR(200));
```

What I saw:

- The sequential table loaded a good bit faster (roughly 2–3× across repeated runs) and finished
  noticeably smaller on disk.
- The fragmentation picture diverged sharply:

  ```sql
  SELECT table_name, data_length, data_free
  FROM information_schema.tables WHERE table_name LIKE 't\_%';
  ```

  The UUID table reported much higher `data_free` and a larger `data_length` for the same row
  count, which lines up with leaf pages left about half full by mid-tree splits. `SHOW ENGINE
  INNODB STATUS` showed many more splits and a lower buffer-pool hit rate during the UUID load,
  because the inserts hit pages scattered across the whole key space instead of one right-most hot
  page.

That's the clustered-index design from §3.1 made visible. Random insertion order forces page
splits and fragmentation; monotonic insertion doesn't.

### 5.2 `SHOW ENGINE INNODB STATUS`

This one command is the diagnostic workhorse. The sections worth reading:

- **BUFFER POOL AND MEMORY** — pool size, free vs database pages, and `Buffer pool hit rate` (e.g.
  `998 / 1000`). The falling hit rate during the §5.1 UUID load showed up right here.
- **LOG** — log sequence number vs "pages flushed up to" vs "last checkpoint at". A big gap means
  checkpointing is falling behind under write pressure.
- **TRANSACTIONS** — active transactions, lock waits, and held locks. This is where you catch a
  long-running transaction that's pinning undo and stopping purge.
- **LATEST DETECTED DEADLOCK** — a full post-mortem of the last deadlock (see §5.4).

### 5.3 `EXPLAIN` of a secondary-index lookup (and covering)

```sql
EXPLAIN SELECT * FROM users WHERE email = 'x@y.com';
-- type: ref   key: idx_email   rows: 1   Extra: (none)
```

`Extra` is empty: InnoDB found the PK via `idx_email`, then did the second lookup into the
clustered index for the full row, which is the double dereference from §3.2. Now restrict the
select list to covered columns:

```sql
EXPLAIN SELECT id, email FROM users WHERE email = 'x@y.com';
-- type: ref   key: idx_email   Extra: Using index
```

`Using index` confirms the query was answered entirely from the secondary index (it already holds
`email` and the PK `id`), skipping the clustered-index fetch. That's the covering-index
optimization showing up in the plan.

### 5.4 Lock waits and deadlocks under concurrent UPDATEs

Two sessions updating the same rows in opposite order:

```sql
-- Session A                          -- Session B
BEGIN;                                BEGIN;
UPDATE acct SET bal=bal-10 WHERE id=1;
                                      UPDATE acct SET bal=bal-10 WHERE id=2;
UPDATE acct SET bal=bal+10 WHERE id=2;  -- waits on B's X-lock on id=2
                                      UPDATE acct SET bal=bal+10 WHERE id=1; -- cycle!
```

InnoDB's deadlock detector caught the cycle and aborted one transaction with
`ERROR 1213 (40001): Deadlock found; try restarting transaction`. The survivor went through right
away. `SHOW ENGINE INNODB STATUS` under *LATEST DETECTED DEADLOCK* listed both transactions, the
exact X-locks each held and waited on, and which one got picked as the victim (usually the one
that had done less work). In a separate run, dropping the isolation level to `READ COMMITTED` cut
the lock contention on range UPDATEs, because gap and next-key locking are mostly off there (§3.6).
Fewer gaps locked, fewer waits, at the price of allowing phantoms.

---

## 6. Key Learnings

- **The table *is* the index.** Once that lands, a lot of InnoDB's behavior explains itself at
  once: why PK lookups are fast, why secondary lookups cost a double dereference, why a fat PK
  quietly bloats every secondary index, and why a random PK wrecks insert performance through page
  splits. Picking a small, monotonic primary key isn't a micro-optimization. It's working with the
  storage engine instead of against it.

- **InnoDB needs both logs because it cheats on flushing.** It changes pages in place and flushes
  them lazily, so it needs redo to roll committed-but-unflushed changes forward and undo to roll
  uncommitted-but-flushed changes back. Redo for durability, undo for atomicity (and MVCC). They
  complement each other rather than overlap.

- **MVCC is where the two engines diverge most.** InnoDB keeps the latest row in the clustered
  index and rebuilds old versions from undo chains; PostgreSQL keeps every version inline in the
  heap and checks visibility with arithmetic on `xmin`/`xmax`. Almost everything else follows from
  that one choice: undo + purge vs single-WAL + `VACUUM`, in-place updates vs append-a-new-tuple,
  compact non-bloating tables vs cheap rollback and a lock-free read path. Neither is "better".
  Each is the consistent result of a different priority.

- **PostgreSQL traded steady-state simplicity for a maintenance task.** Inline versioning gives it
  a trivial read-visibility check, near-free rollback, and no undo-segment contention or "snapshot
  too old", at the price of heap bloat that `VACUUM` reclaims in the background. InnoDB took the
  opposite trade: compact non-bloating storage and fast PK access, paid for with secondary-index
  double lookups, page-split sensitivity, and a purge thread.

- **Isolation is implemented twice over.** InnoDB mixes MVCC snapshots (lock-free consistent reads)
  with pessimistic next-key locking to kill phantoms at the default REPEATABLE READ, which is
  stronger than the SQL standard requires. PostgreSQL reaches comparable guarantees with snapshots
  plus optimistic SSI at SERIALIZABLE. Same destination, opposite philosophies: block now vs abort
  later.

- **The buffer pool hides a lot of careful engineering.** Midpoint-insertion LRU resists scan
  pollution, the change buffer turns random secondary-index writes into deferred batched I/O, and
  the adaptive hash index self-tunes hot lookups. Each one is a targeted answer to a specific
  workload problem.

---

## References

- **MySQL 8.0 Reference Manual — Chapter 17, "The InnoDB Storage Engine."** Oracle Corporation.
  The authoritative primary source for clustered/secondary index structure, buffer pool internals,
  redo/undo logs, locking, and isolation levels.
  <https://dev.mysql.com/doc/refman/8.0/en/innodb-storage-engine.html>
- **Jeremy Cole, "InnoDB" blog series**, *blog.jcole.us* — deep, diagram-rich reverse-engineering of
  the InnoDB on-disk page and B+tree layout (e.g. "The physical structure of InnoDB index pages,"
  "B+Tree index structures in InnoDB"). Excellent for understanding how rows, records, and pages are
  actually laid out. <https://blog.jcole.us/innodb/>
- **Schwartz, Zaitsev & Tkachenko, *High Performance MySQL* (O'Reilly).** The standard practitioner
  reference on schema/index design, the clustered-index implications, and performance tuning of the
  buffer pool and transaction logs.
- **PostgreSQL Documentation — "Concurrency Control" (MVCC) and "Internals / Database Physical
  Storage."** Used here for the comparison points: heap storage, `xmin`/`xmax` tuple visibility,
  `VACUUM`, and Serializable Snapshot Isolation.
  <https://www.postgresql.org/docs/current/mvcc.html>
