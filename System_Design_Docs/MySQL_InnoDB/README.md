# MySQL InnoDB Storage Engine

An analysis of how InnoDB stores and processes data underneath MySQL's SQL layer:
how rows physically live inside a clustered B-tree, how undo and redo logs split
the two jobs of "let me roll back / read old versions" and "let me survive a
crash," and why InnoDB's locking includes the unusual gap and next-key locks. The
document is written as a comparison study: at each step it contrasts InnoDB's
choices with PostgreSQL's, because the two engines made nearly opposite decisions
from the same relational starting point, and the contrast is what makes each
decision legible.

---

## 1. Problem Background

**Why InnoDB was created.** Early MySQL shipped with MyISAM as its workhorse table
type. MyISAM was fast for read-heavy workloads and simple to operate, but it had
two disqualifying gaps for serious transactional applications: it offered **no
transactions** (no `COMMIT`/`ROLLBACK`, no ACID) and only **table-level locking**,
so a single writer blocked every other reader and writer on that table. It was also
not crash-safe — an unclean shutdown could leave tables corrupt, repairable only by
a full table scan. InnoDB was developed (by Innobase Oy, Heikki Tuuri) to give
MySQL what MyISAM lacked: ACID transactions, row-level locking, multi-version
concurrency control, and crash recovery via logging.

**Why MySQL adopted it as the default.** As MySQL moved from a fast website backend
into general OLTP and commerce, the inability to do safe concurrent writes and to
survive crashes became the dominant pain point. InnoDB solved exactly those, so it
steadily became the engine of choice and was made the **default storage engine in
MySQL 5.5 (2010)**. The "pluggable storage engine" architecture is itself a
distinctive MySQL design decision: the SQL layer (parser, optimizer, caches) is
separated from the storage engine by a well-defined handler API, so engines like
InnoDB, MyISAM, and MEMORY can coexist under one SQL dialect. InnoDB winning the
default slot reflects that transactional correctness had become non-negotiable.

**Problems InnoDB was designed to solve.**

- *Concurrency:* many sessions writing different rows of the same table without
  blocking each other → row-level locking + MVCC.
- *Durability and crash safety:* committed data must survive power loss, and
  recovery must be fast (no full table repair) → redo logging + checkpoints.
- *Atomicity / rollback:* a transaction must be undoable, and readers must see a
  consistent snapshot → undo logging.
- *Efficient primary-key access:* OLTP workloads constantly look up and range-scan
  by primary key → clustered storage that puts the row data in the PK B-tree.

**Historical context.** InnoDB predates its time at MySQL AB as a third-party
engine, was used by large sites well before becoming default, and passed through
Oracle's acquisition of Innobase (2005) and then of MySQL/Sun (2010). Its lineage
explains its character: it borrows heavily from classic Oracle-style transaction
design — clustered (index-organized) tables, rollback segments / undo for
consistent reads, and ARIES-influenced redo logging — rather than from PostgreSQL's
heap-plus-vacuum lineage.

---

## 2. Architecture Overview

MySQL is layered: a shared SQL front end sits on top of a pluggable storage engine.
InnoDB is the engine that actually owns the data, the in-memory buffer pool, and the
log files.

```
        Client (app / mysql CLI)
              |
              |  MySQL client/server protocol
              v
 +-----------------------------------------------------+
 |                 MySQL SQL LAYER                      |
 |   connection mgmt | parser | optimizer | exec       |
 |   (engine-agnostic; talks via handler API)          |
 +--------------------------+--------------------------+
                            |  handler API (row ops)
                            v
 +-----------------------------------------------------+
 |                  InnoDB STORAGE ENGINE              |
 |                                                     |
 |   +-------------------- IN MEMORY ---------------+   |
 |   |  Buffer Pool (cached 16KB pages, LRU)       |   |
 |   |  +-----------+  +-----------+  +----------+  |   |
 |   |  |  data /   |  | change    |  |  adaptive|  |   |
 |   |  |  index pgs|  | buffer    |  | hash idx |  |   |
 |   |  +-----------+  +-----------+  +----------+  |   |
 |   |  Log Buffer (redo records, not yet flushed) |   |
 |   +---------------------------------------------+   |
 |                       |  flush dirty pages          |
 |                       |  / write redo               |
 +-----------------------|-----------------------------+
                         v
 +-----------------------------------------------------+
 |                 ON DISK (tablespaces)               |
 |  *.ibd files: clustered + secondary B-trees (16KB)  |
 |  ib_logfile / #innodb_redo : REDO log (sequential)  |
 |  undo tablespaces : UNDO log (old row versions)     |
 |  ibdata1 : system tablespace / data dictionary      |
 +-----------------------------------------------------+
```

**Query flow.** A statement arrives at the SQL layer, which parses and optimizes it
into a plan, then calls down through the handler API to InnoDB for row-level
operations ("fetch the row with this PK", "scan this index range", "insert this
row"). InnoDB serves those from the buffer pool, reading pages from the `.ibd`
tablespace on a miss, and recording changes in undo and redo logs.

**Storage layer.** Each table is a set of B-trees stored in 16 KB pages inside a
tablespace file (`table.ibd` under the default file-per-table mode). The *clustered
index* (keyed by primary key) holds the actual rows; *secondary indexes* are
separate B-trees that point back into it. The system tablespace and dedicated undo
tablespaces hold transaction metadata and old row versions.

**Buffer pool interaction.** The buffer pool is InnoDB's in-memory cache of 16 KB
pages and the center of gravity for performance. Reads are served from it; writes
modify pages in it (marking them dirty) and are flushed lazily. On a well-sized
server the buffer pool holds most of the working set, so most operations never touch
disk synchronously.

**Transaction processing flow.** `BEGIN` starts a transaction and assigns it
resources (a transaction id, undo slots). Each modification writes an *undo record*
(how to reverse it / how older readers should see the prior version) and a *redo
record* (how to replay it after a crash), then mutates the page in the buffer pool.
`COMMIT` flushes the redo log up to the commit record (`fsync`) — that flush is the
durability point — while the dirty data pages themselves are written back later by
background flushing.

---

## 3. Internal Design

### Clustered Indexes

**Primary-key storage.** InnoDB tables are *index-organized*: every table **is** a
B-tree keyed by the primary key, and the full row lives in the leaf node of that
B-tree. There is no separate unordered heap as in PostgreSQL. If a table has no
explicit primary key, InnoDB uses the first non-null `UNIQUE` index, or else
synthesizes a hidden 6-byte `DB_ROW_ID`.

**Clustered organization and data pages.** Because the leaves are ordered by primary
key, rows that are adjacent in PK order are physically adjacent on disk (within and
across 16 KB pages linked in key order).

```
   Clustered index (PK = id)

                       [ root page ]
                       | 1 | 41 | 90 |
                        /     |     \
              [internal]  [internal]  [internal]
                 ...          ...        ...
                  |            |          |
            +-----------+-----------+-----------+
            | leaf page | leaf page | leaf page |   (pages linked in PK order)
            +-----------+-----------+-----------+
            | id=1  -> FULL ROW (all columns)  |
            | id=2  -> FULL ROW                |
            | id=3  -> FULL ROW                |   <- the leaf level *is* the table
            +----------------------------------+
```

**Page structure.** A 16 KB InnoDB page has a file header and trailer (checksum,
page number, LSN), a page header tracking record counts and free space, the user
records stored in a singly linked list in key order, a set of "page directory"
slots that allow binary search within the page, system records (infimum/supremum
sentinels marking the low/high ends), and a free-space area. Each clustered-index
record also carries hidden system columns: a transaction id (`DB_TRX_ID`) and a
rollback pointer (`DB_ROLL_PTR`) — the hooks that drive MVCC (below).

**Why clustered indexes improve lookups.** A primary-key lookup descends the B-tree
and finds the *entire row* right there in the leaf — one traversal, no second fetch.
Range scans and ordered reads by PK (`WHERE id BETWEEN …`, `ORDER BY id`) are
near-sequential page reads because the data is already stored in that order. This is
ideal for OLTP, where access is overwhelmingly by primary key. The cost (discussed
later) is that secondary-index access needs an extra step, and that PK choice has
outsized physical consequences.

### Secondary Indexes

**Structure.** A secondary index is its own B-tree, keyed by the indexed column(s).
Crucially, its leaf entries do **not** store a physical row pointer — they store the
**primary key value** of the row.

```
   Secondary index on (email)            Clustered index (PK=id)
   +--------------------------+          +--------------------------+
   | leaf:                    |          | leaf:                    |
   |  'a@x' -> PK 42          |  ---->   |  id=42 -> FULL ROW       |
   |  'b@x' -> PK 7           |  (then   |  id=7  -> FULL ROW       |
   |  'c@x' -> PK 88          |  look up |  id=88 -> FULL ROW       |
   +--------------------------+   by PK) +--------------------------+
```

**Lookup path.** Querying by a secondary key is a **two-phase** operation: (1)
descend the secondary B-tree to find the matching primary key value(s); (2) descend
the *clustered* B-tree by that PK to fetch the full row. This second hop is called a
**bookmark lookup** (or "table lookup").

**Cost compared with the clustered index.** A clustered (PK) lookup is one B-tree
traversal. A secondary lookup is effectively *two* traversals per matching row. The
mitigation is the **covering index**: if every column the query needs is already
present in the secondary index entry (the indexed columns plus the PK it carries),
InnoDB can answer entirely from the secondary B-tree and skip the clustered lookup —
which is why adding columns to an index to "cover" a hot query is a common
optimization. A consequence worth remembering: because secondary leaves store the
PK, a **wide primary key bloats every secondary index** (each entry carries a copy
of it), which is the main argument for keeping PKs small.

### Buffer Pool

**Page caching.** The buffer pool caches 16 KB pages in RAM. On access, InnoDB
checks the buffer pool's hash table; a hit returns the in-memory page, a miss reads
it from the tablespace into a free (or evicted) frame.

**Dirty pages and flushing.** A write modifies the page *in memory* and marks it
**dirty**; it is not written to its data file immediately. Background threads flush
dirty pages to disk over time — driven by checkpoint progress, buffer-pool pressure,
and the configured I/O capacity — so that random data-page writes are batched and
smoothed rather than done synchronously at commit. Durability at commit comes from
the redo log, not from flushing the data page (see Redo Logs).

**LRU replacement.** InnoDB uses a **midpoint-insertion LRU**: the list is split
into a "young" (hot) sublist and an "old" sublist, and newly read pages are inserted
at the *head of the old* sublist rather than the very top. A page is only promoted to
the young sublist if it is accessed again after a short time window. This guards
against a large one-off scan (e.g., a big range read or a backup) flooding the cache
and evicting the genuinely hot working set — a classic problem with naïve LRU.

**How reads and writes interact with it.** Reads pull pages in and warm the LRU.
Writes dirty pages in place and rely on later flushing. Two related mechanisms ride
on the buffer pool: the **change buffer**, which defers and merges secondary-index
modifications for pages not currently cached (avoiding random read-modify-write of
secondary indexes), and the **adaptive hash index**, which auto-builds an in-memory
hash over frequently accessed pages to shortcut B-tree descents.

### Transaction Processing

**ACID.** InnoDB provides all four: **Atomicity** via undo logs (an incomplete
transaction can be fully reversed), **Consistency** via constraint enforcement and
the other three properties, **Isolation** via MVCC + locking (default isolation
level `REPEATABLE READ`), and **Durability** via redo logs flushed at commit.

**Transaction lifecycle.**

```
BEGIN
  -> assign transaction id; reserve undo log slots
STATEMENT (INSERT / UPDATE / DELETE)
  -> write UNDO record (prior image / how to reverse)
  -> write REDO record (how to replay the change)
  -> modify the page in the buffer pool (mark dirty)
  -> acquire row / next-key locks as needed
COMMIT
  -> write commit record to redo log buffer
  -> flush redo log to disk (fsync)  <-- durability point
  -> release locks; undo kept only as long as some snapshot may need it
(or) ROLLBACK
  -> apply undo records in reverse to restore prior state
```

**Commit flow.** The essential point is that commit is made durable by forcing the
**redo log** (small, sequential), governed by `innodb_flush_log_at_trx_commit`
(=1 means flush+fsync every commit, the fully durable setting). The modified data
pages are still only in the buffer pool at that moment; they will be flushed later,
and if a crash intervenes, redo replay reconstructs them.

### Undo Logs

**Purpose.** The undo log stores the *previous* version of any row a transaction
changes. It serves two distinct jobs at once:

1. **Rollback** — if the transaction aborts (or errors), InnoDB walks its undo
   records in reverse and restores each row to its prior state.
2. **MVCC / consistent reads** — readers that started before this change must still
   see the *old* version; the undo records are precisely those old versions, so they
   are the source of "snapshot" data for other transactions.

**Rollback operations.** Each modified clustered-index record holds a `DB_ROLL_PTR`
pointing to an undo record describing the previous version; that undo record may in
turn point to an older one, forming a **version chain**.

**MVCC support — example version chain.** Suppose row `id=10` starts as `bal=100`,
then transaction T20 sets it to `150`, then T35 sets it to `170`:

```
Clustered-index record (current, in the data page):
   [ id=10 | bal=170 | DB_TRX_ID=35 | DB_ROLL_PTR -> U2 ]
                                                    |
   undo log:                                        v
        U2: { prior bal=150, made by TRX 20, DB_ROLL_PTR -> U1 }
                                                    |
                                                    v
        U1: { prior bal=100, made by TRX <20,  DB_ROLL_PTR -> (none) }
```

When a transaction reads `id=10`, InnoDB compares the record's `DB_TRX_ID` against
the reader's **read view** (the set of transaction ids considered visible). If the
current version was created by a transaction the reader should not see, InnoDB
follows `DB_ROLL_PTR` down the chain until it finds a version the read view allows —
reconstructing exactly the snapshot that reader is entitled to. This is why InnoDB
needs *neither* extra heap versions *nor* a VACUUM: old versions live in the undo
log, not interleaved in the table, and a background **purge** thread discards undo
records once no read view can still need them.

### Redo Logs

**Purpose and durability.** The redo log is a fixed-size, circularly reused,
**sequentially written** on-disk log of physical(-logical) page changes ("on page P
at offset O, bytes changed to X"). Its job is durability: a committed transaction's
changes are guaranteed to be reconstructible even though the data pages themselves
may not yet be on disk.

**Write path.** Changes go first into the in-memory **log buffer**, then to the redo
log files. At commit, InnoDB writes the commit record and (with the durable setting)
**fsyncs** the redo log. This is the *write-ahead* principle: the redo record for a
page change is durable before the dirty data page is allowed to be written to its
final location, enforced via each page's LSN versus the log's flushed LSN.

**Crash recovery.** On restart after a crash, InnoDB:

```
   1. find the last checkpoint LSN
   2. REDO phase: scan redo log forward from the checkpoint, re-applying
      every change whose page LSN is older than the log record (idempotent)
   3. UNDO phase: roll back any transactions that were active but never
      committed (using the undo logs)
   -> database is consistent; open for connections
```

The redo pass replays committed-but-not-yet-flushed work; the undo pass removes the
effects of in-flight transactions. Checkpoints periodically record how far the data
files are caught up, bounding how much redo must be scanned.

**Why redo logging is necessary.** Without it, durability would require flushing
every modified data page to its (random) location at commit — slow and full of small
random writes. Redo turns the commit-time cost into a single sequential append +
fsync, while letting the large random data writes happen lazily and in batches in
the background. It is the mechanism that makes "fast commits + lazy data writes +
crash safety" simultaneously possible.

### Locking

InnoDB locks at the **row** level (actually, on index records), which is what lets
many writers work on different rows of one table concurrently.

- **Shared (S) locks** — held by readers that need to lock rows (e.g.,
  `SELECT … LOCK IN SHARE MODE`); multiple S locks on the same row coexist.
- **Exclusive (X) locks** — held by writers (`UPDATE`/`DELETE`, `SELECT … FOR
  UPDATE`); an X lock conflicts with all other S/X locks on that row.
- **Record locks** — a lock on a single index record.
- **Gap locks** — a lock on the *gap between* index records (an open interval that
  currently contains no row), preventing other transactions from *inserting* into
  that gap.
- **Next-key locks** — the InnoDB default under `REPEATABLE READ`: a record lock on
  an index record **plus** a gap lock on the gap before it, i.e., it locks the row
  and the range leading up to it.

**Why gap locks exist.** They prevent **phantom reads**. Consider a transaction that
runs `SELECT … WHERE x BETWEEN 10 AND 20 FOR UPDATE` and finds three rows. Without
gap locking, another transaction could `INSERT` a new row with `x=15` and commit;
re-running the range query would now return a fourth row — a phantom. By locking the
*gaps* in the scanned range (via next-key locks), InnoDB blocks such inserts,
delivering `REPEATABLE READ` that is effectively phantom-free for locking reads. The
trade-off is reduced insert concurrency in the locked range and a class of deadlocks
and `LOCK WAIT` situations specific to gap locking — which is why lowering isolation
to `READ COMMITTED` (which largely disables gap locks) is a common tuning move for
insert-heavy workloads.

---

## 4. PostgreSQL vs InnoDB Comparison

The two engines diverge at almost every layer. The tables below pair each choice
with the reasoning behind it.

**Storage organization**

| Aspect              | PostgreSQL                              | InnoDB                                    |
|---------------------|-----------------------------------------|-------------------------------------------|
| Table layout        | Unordered **heap** of 8 KB pages        | **Clustered** B-tree (16 KB), keyed by PK |
| Row location        | TID `(block, offset)`                    | Position in PK B-tree leaf                 |
| PK access           | Index → heap fetch (two structures)     | Single B-tree descent to the row          |
| Implication         | All indexes are equal (secondary)       | PK is privileged; secondary needs a hop   |

*Why:* PostgreSQL keeps storage simple and uniform (a heap plus equal secondary
indexes), which makes updates that move data cheap. InnoDB optimizes the dominant
OLTP pattern — PK lookups and PK range scans — by storing rows in PK order.

**MVCC implementation**

| Aspect              | PostgreSQL                              | InnoDB                                    |
|---------------------|-----------------------------------------|-------------------------------------------|
| Where old versions live | In the **table heap** (extra tuples) | In the **undo log**, off to the side       |
| Version pointer     | `xmin`/`xmax` on each tuple             | `DB_TRX_ID` + `DB_ROLL_PTR` chain          |
| Visibility check    | Snapshot vs `xmin`/`xmax` + CLOG        | Read view vs `DB_TRX_ID`, walk undo chain  |

*Why:* both achieve "readers don't block writers," but PostgreSQL stores versions
inline (simple writes, but the table accumulates dead tuples), while InnoDB keeps
the table holding only the current row and pushes history into undo (clean table,
but the undo log must be maintained and purged).

**Updates**

| Aspect              | PostgreSQL                              | InnoDB                                    |
|---------------------|-----------------------------------------|-------------------------------------------|
| Update mechanism    | Delete old tuple + **insert new tuple** | **In-place** update; old image to undo     |
| Index impact        | New tuple ⇒ new index entries (unless HOT) | Only changed indexes touched            |
| Space behavior      | Table grows with dead tuples            | Table stays compact; undo grows then purged |

*Why:* PostgreSQL's append-style updates avoid touching the old tuple, simplifying
concurrency, at the cost of bloat. InnoDB updates the row where it sits and records
the before-image in undo, keeping the clustered index dense.

**Cleanup mechanisms**

| Aspect              | PostgreSQL                              | InnoDB                                    |
|---------------------|-----------------------------------------|-------------------------------------------|
| Garbage             | Dead tuples in heap and indexes         | Stale undo records                         |
| Reclaimer           | **VACUUM / autovacuum**                  | **Purge** thread(s)                        |
| Visibility / wraparound | Visibility map; freezing to avoid txid wraparound | Purge bounded by oldest read view |

*Why:* same fundamental need (collect versions no one can see) located in different
places — PostgreSQL must sweep the *table itself*; InnoDB only sweeps the *undo log*.

**Index architecture**

| Aspect              | PostgreSQL                              | InnoDB                                    |
|---------------------|-----------------------------------------|-------------------------------------------|
| Secondary leaf points to | Heap TID (physical address)        | **Primary key value** (logical)            |
| Effect of row move  | Indexes can need updating / HOT helps   | Row stays addressable by PK; no re-point   |
| PK width cost       | Independent of PK width                 | Wide PK **bloats every secondary index**   |

*Why:* InnoDB's "secondary → PK" indirection means rows can be reorganized within
the clustered index without invalidating secondary indexes, but it pays an extra
lookup and embeds the PK everywhere.

**Locking behavior**

| Aspect              | PostgreSQL                              | InnoDB                                    |
|---------------------|-----------------------------------------|-------------------------------------------|
| Granularity         | Row locks via tuple, plus many lock modes | Index-record, gap, and next-key locks    |
| Phantom protection  | Serializable via SSI (dependency tracking) | Gap/next-key locks under REPEATABLE READ |
| Default isolation   | Read Committed                          | Repeatable Read                            |

*Why:* PostgreSQL prevents anomalies analytically (SSI detects dangerous read/write
cycles); InnoDB prevents phantoms physically (lock the gaps). Different philosophies:
detect-and-abort vs lock-and-block.

**Recovery mechanisms**

| Aspect              | PostgreSQL                              | InnoDB                                    |
|---------------------|-----------------------------------------|-------------------------------------------|
| Durability log      | WAL (redo)                              | Redo log                                   |
| Undo of uncommitted | Implicit (uncommitted tuples never visible via MVCC) | Explicit **undo** pass at recovery |
| Recovery shape      | Replay WAL from checkpoint              | Redo replay + undo rollback (ARIES-style)  |

*Why:* because PostgreSQL leaves uncommitted versions in the heap and relies on
visibility to hide them, it needs no separate undo pass; InnoDB updates in place, so
it must actively roll back interrupted transactions during recovery.

**Summary of the two philosophies.** PostgreSQL: *heap storage* + *inline tuple
versioning* + *VACUUM* — keep writes simple and pay later by cleaning the table.
InnoDB: *clustered storage* + *undo logs* + *in-place updates* — keep the table
compact and PK access fast, and pay by maintaining/purging a separate undo history
and by carrying the secondary-index indirection.

---

## 5. Experiments / Observations

These are architectural observations, not benchmarks.

**EXPLAIN output discussion.** MySQL's `EXPLAIN` reveals which index strategy the
optimizer chose. Key columns:

```sql
EXPLAIN SELECT * FROM users WHERE email = 'a@x';
```

- `type = ref` with `key = idx_email` means a secondary-index lookup — remember this
  implies the two-phase path (secondary B-tree → clustered B-tree).
- `type = eq_ref`/`const` on the primary key means a single clustered descent.
- `Extra: Using index` is the tell-tale of a **covering index**: the query was
  satisfied entirely from the secondary index, so the clustered bookmark lookup was
  skipped. Its *absence* on a secondary-key query means the extra hop happened.
- `type = ALL` means a full clustered-index scan (no usable index).

The reasoning value of EXPLAIN here is that it makes the clustered-vs-secondary cost
model visible: you can literally see when InnoDB had to pay for the second lookup
and when a covering index let it avoid it.

**Clustered index lookup behavior.** A query filtering and ordering by primary key
(`WHERE id BETWEEN 1000 AND 2000 ORDER BY id`) reads adjacent leaf pages in order —
sequential-ish I/O and no sort step, because the clustered index already stores rows
in PK order. The same query ordered by a non-PK column generally needs a sort or a
different index.

**Secondary index lookup path.** A non-covering secondary query visibly does more
work per row: for each match in the secondary index it must descend the clustered
index again. With many matches this amplifies into many clustered descents, which is
why a highly selective secondary index (few matches) is far more valuable than a
weakly selective one, and why covering indexes pay off on hot read paths.

**Effects of locking.** Two transactions updating *different* rows proceed
concurrently (row locks). Two updating the *same* row serialize (X-lock conflict).
Under `REPEATABLE READ`, a `SELECT … FOR UPDATE` over a range takes next-key locks,
so a concurrent `INSERT` into that range *blocks* — directly observable as the
insert waiting on a lock, and the mechanism behind both phantom prevention and a
class of insert-time deadlocks. Switching the session to `READ COMMITTED` removes
most gap locking and the insert proceeds — a concrete demonstration of the
isolation/concurrency trade-off.

**Effects of transactions.** Inside one transaction under `REPEATABLE READ`,
re-reading a row returns the same value even if another committed transaction
changed it meanwhile — because the read view was fixed at first read and InnoDB
serves the older version from the undo chain. The same experiment under `READ
COMMITTED` shows the new value on the second read, since a fresh read view is taken
per statement. This makes the undo-driven MVCC mechanism observable from SQL alone.

---

## 6. Design Trade-Offs

**Clustered index**

| Benefits                                          | Drawbacks                                            |
|---------------------------------------------------|------------------------------------------------------|
| PK lookups land on the full row in one descent    | Secondary lookups need a second (PK) descent         |
| PK range scans / ordered reads are near-sequential| Wide PK bloats every secondary index                 |
| No separate heap; rows physically clustered       | Random-PK inserts (e.g., UUIDs) cause page splits & fragmentation |
| Great fit for OLTP PK-centric access              | PK choice has large, hard-to-change physical impact  |

**Undo log overhead**

| Benefit                                           | Cost                                                 |
|---------------------------------------------------|------------------------------------------------------|
| Enables rollback **and** MVCC from one structure  | Must be written for every modification               |
| Keeps the table compact (history lives elsewhere) | Long-running read views block purge → undo bloats    |
| No table-level VACUUM needed                       | Reads of old versions chase pointer chains           |

**Redo log overhead**

| Benefit                                           | Cost                                                 |
|---------------------------------------------------|------------------------------------------------------|
| Durable commits without random data-page flushes  | Every change written twice (redo + eventual page)    |
| Fast, sequential write + fsync at commit          | `fsync` per commit adds latency (tunable, less safe) |
| Bounded, fast crash recovery                      | Fixed-size redo too small → aggressive flushing stalls|

**Locking trade-offs**

| Benefit                                           | Cost                                                 |
|---------------------------------------------------|------------------------------------------------------|
| Row-level locks → high write concurrency          | Many locks to track; deadlocks possible              |
| Gap/next-key locks → phantom-free REPEATABLE READ | Reduced insert concurrency in locked ranges          |
| Tunable via isolation level                        | Correctness/concurrency must be balanced per workload|

**Comparison with PostgreSQL (where each pays).** PostgreSQL pays at *cleanup time*
(VACUUM must sweep the whole table and manage wraparound) and on *secondary-free
storage* (heap + equal indexes), but enjoys cheap data movement and no
secondary-index indirection. InnoDB pays at *secondary access* (extra hop) and in
*undo/purge maintenance*, but enjoys compact tables and fast PK access. Neither is
universally better; they optimize different access patterns and accept different
maintenance burdens.

---

## 7. Key Learnings

- **Most important InnoDB concepts.** The table *is* a clustered B-tree keyed by the
  primary key; secondary indexes point to PK values, not physical addresses; undo
  logs power both rollback and MVCC; redo logs power durability and crash recovery;
  locking is row/gap/next-key based. These five ideas explain almost all of InnoDB's
  behavior.
- **Why clustered indexes matter.** They make the dominant OLTP operation — PK
  lookup and PK range scan — a single ordered B-tree traversal that returns the whole
  row, at the cost of a second hop for secondary access and a strong sensitivity to
  PK width and PK insertion order. Choosing a small, monotonic primary key is
  therefore a high-impact design decision in InnoDB specifically.
- **Why both undo and redo logs are required.** They solve *different* problems and
  point in *opposite* directions. Undo holds *old* images to move backward (rollback)
  and sideways (let other readers see prior versions for MVCC). Redo holds *new*
  changes to move forward (replay committed work after a crash). One cannot do the
  other's job: redo can't reconstruct a consistent read snapshot, and undo can't
  recover changes that never reached the data files. Recovery uses both — redo to
  replay, undo to roll back the unfinished.
- **Differences from PostgreSQL.** Same goals (ACID, MVCC, crash safety), opposite
  mechanics: heap + inline versioning + VACUUM versus clustered storage + undo +
  in-place updates; secondary→TID versus secondary→PK; detect-and-abort (SSI) versus
  lock-the-gap phantom protection; implicit visibility-based undo versus an explicit
  recovery-time undo pass.
- **Engineering lessons.** Storage-engine design is a system of paired
  decisions: every optimization plants a maintenance cost somewhere else, and the
  art is choosing *where* to pay. InnoDB pushes the cost toward secondary access and
  undo purge in exchange for compact, PK-fast tables; PostgreSQL pushes it toward
  VACUUM in exchange for simple, uniform storage. Reading either engine well means
  tracing each feature to the cost it incurs and the workload that makes that cost
  worthwhile.

---

## References

1. Oracle — *MySQL Reference Manual*, "The InnoDB Storage Engine" (architecture,
   clustered/secondary indexes, buffer pool, redo/undo logs, locking, transaction
   model). https://dev.mysql.com/doc/refman/8.0/en/innodb-storage-engine.html
2. Oracle — *MySQL Reference Manual*, "InnoDB Locking and Transaction Model"
   (shared/exclusive, record, gap, and next-key locks; isolation levels).
   https://dev.mysql.com/doc/refman/8.0/en/innodb-locking.html
3. Oracle — *MySQL Reference Manual*, "InnoDB On-Disk Structures" and "InnoDB
   In-Memory Structures" (page format, tablespaces, buffer pool LRU, change buffer).
4. J. Gray, A. Reuter — *Transaction Processing: Concepts and Techniques* (undo/redo
   logging, recovery principles).
5. C. Mohan et al. — "ARIES: A Transaction Recovery Method…", ACM TODS, 1992
   (the write-ahead logging / redo+undo recovery model InnoDB follows).
6. B. Schwartz, P. Zaitsev, V. Tkachenko — *High Performance MySQL* (InnoDB
   internals, clustered index implications, locking and isolation in practice).
7. PostgreSQL Global Development Group — *PostgreSQL Documentation*: "Concurrency
   Control (MVCC)", "Routine Vacuuming", "Write-Ahead Logging" (for the comparison).
