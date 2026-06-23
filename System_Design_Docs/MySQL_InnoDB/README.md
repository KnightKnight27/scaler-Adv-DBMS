# MySQL / InnoDB Storage Engine

> Advanced DBMS, System Design Discussion
> MySQL was not installed in my environment, so the SQL/EXPLAIN examples in section 5 are illustrative. They reflect documented InnoDB behaviour (MySQL 8.0/8.4 manual) rather than live captures. The architectural analysis is the main focus.

---

## 1. Problem Background

MySQL is the most popular open-source SQL database for web applications, and **InnoDB** has been its default storage engine since MySQL 5.5 (2010). MySQL has a pluggable storage-engine design: the SQL layer parses and optimizes queries, while the storage engine actually stores and retrieves rows. The older default, **MyISAM**, was fast for reads but had no transactions, no crash recovery, and only table-level locking, which is not acceptable for OLTP. InnoDB exists to provide what MyISAM could not:

- ACID transactions with commit and rollback,
- crash recovery, so a committed transaction survives a power loss,
- row-level locking for high concurrency,
- MVCC, so reads do not block writes.

The big design questions InnoDB answers (how to store rows, how to undo and redo, how to lock) it answers very differently from PostgreSQL, which makes it a good contrast study.

---

## 2. Architecture Overview

```
   SQL layer (parser, optimizer)  ── handle  ─►  InnoDB storage engine
                                                       │
   ┌─────────────────── In memory ──────────────────────────────────┐
   │  BUFFER POOL (caches 16KB pages)                                 │
   │     LRU list:  [ young / new  5/8 ] [ old  3/8 ]  midpoint ins.  │
   │  Change buffer · Log buffer · Adaptive hash index                │
   └───────────────┬───────────────────────────┬─────────────────────┘
                   │ flush dirty pages          │ WAL (log first)
                   ▼                            ▼
   ┌──────────────────────────────┐   ┌────────────────────────────┐
   │  Tablespaces (.ibd files)     │   │  REDO LOG  (#innodb_redo/)  │  durability /
   │   ┌────────────────────────┐  │   │  physiological, by LSN      │  crash recovery
   │   │ Clustered index B+tree  │  │   └────────────────────────────┘
   │   │  (= the table itself)   │  │   ┌────────────────────────────┐
   │   ├────────────────────────┤  │   │  UNDO LOGS (rollback segs)  │  rollback + MVCC
   │   │ Secondary index B+trees │  │   │  old row versions           │  old versions
   │   └────────────────────────┘  │   └────────────────────────────┘
   │  Doublewrite buffer (torn-page protection)                       │
   └──────────────────────────────────────────────────────────────────┘
```

**Data flow.** A query is parsed and optimized by the SQL layer and handed to InnoDB. InnoDB serves pages from the buffer pool (16 KB pages). Modifications are written to the redo log first (WAL) and applied to the in-memory pages, while the prior row version is saved to the undo log (for rollback and MVCC). Dirty pages are flushed to the tablespace later, going through the doublewrite buffer to survive torn writes.

---

## 3. Internal Design

### 3.1 Clustered index: the table is a B+tree

The single most important InnoDB fact is that every table is physically stored as a B+tree keyed on its primary key, and the leaf nodes contain the entire row. The table and its primary key are the same structure, called the clustered index. There is exactly one per table.

How the clustering key is chosen:
1. the PRIMARY KEY if you defined one;
2. otherwise the first UNIQUE NOT NULL index;
3. otherwise InnoDB invents a hidden 6-byte auto-increment `DB_ROW_ID` and clusters on it (`GEN_CLUST_INDEX`).

```
 Clustered index (PK = id)              leaves hold FULL rows, sorted by PK
        [ 50 | 200 ]            (internal: separator keys + child pointers)
       /      |      \
 [1..49] [50..199] [200..]      ← leaf pages: (id, name, email, …whole row…)
```

Because rows are stored in primary-key order, a PK range scan (`WHERE id BETWEEN …`) reads sequential pages, which is very fast.

### 3.2 Secondary indexes, and why they need a double lookup

A secondary index is a separate B+tree, but its leaves do not store a physical row pointer. They store the indexed columns plus the primary-key value. To fetch the full row, InnoDB looks up the PK in the secondary index, then does a second lookup in the clustered index by that PK:

```
SELECT * FROM users WHERE email = 'a@b.com';
   1) walk email-index B+tree → finds (email='a@b.com' → pk=42)
   2) walk clustered index by pk=42 → fetches the full row    ← double lookup
```

A covering index avoids step 2: if every column the query needs is already in the secondary index, InnoDB answers from the index alone (`Using index` in EXPLAIN). Two things worth remembering:
- keep the primary key short, because its value is copied into every secondary index;
- a secondary lookup is two B+tree traversals unless it is covering.

### 3.3 Page structure and buffer pool

InnoDB's page is 16 KB by default (`innodb_page_size`, fixed at instance init). Each page has a 38-byte FIL header and an 8-byte FIL trailer (which holds a checksum plus the low bytes of the LSN for tear detection). Index pages are doubly linked to their siblings at the same B+tree level for range scans.

The buffer pool (`innodb_buffer_pool_size`) caches these pages. Its LRU list has a clever twist to stay scan-resistant:

```
   LRU list:   [────── young (hot) ~5/8 ──────][──── old ~3/8 ────]
                                                ▲ midpoint
   A newly read page is inserted at the MIDPOINT (head of the old list),
   not the absolute head. It is only promoted into the "young" region if
   it is accessed AGAIN after innodb_old_blocks_time (default 1000 ms).
```

This means a one-off full table scan (which reads many pages once) parks them in the old sublist and they age out quickly, instead of evicting the genuinely hot working set. `innodb_old_blocks_pct` defaults to 37 (about 3/8).

The change buffer is another optimization: modifications to secondary-index pages that are not currently in the buffer pool are buffered and merged later, turning random secondary-index I/O into batched I/O.

### 3.4 Redo log, undo log, and crash recovery

InnoDB keeps two logs that do opposite jobs. This is a frequent point of confusion, so here it is plainly:

| | Redo log | Undo log |
|---|---|---|
| Question it answers | "redo committed changes lost in the crash" | "undo uncommitted changes / show old versions" |
| Direction | roll forward | roll back |
| Used for | durability and crash recovery | rollback and MVCC reads |
| Stored | `#innodb_redo/` (8.0.30+) by LSN | rollback segments in undo tablespaces |

- **Redo log (WAL).** Before a dirty page is flushed, the change is recorded in the redo log and the log is forced to disk at commit. Redo is physiological (which page, plus a logical description of the change within it), identified by LSN. On restart, InnoDB scans from the last checkpoint LSN and replays redo forward to recover committed-but-unflushed work. `innodb_flush_log_at_trx_commit=1` (default) gives full ACID (fsync every commit), while `=2` fsyncs about once a second (faster, can lose up to 1 second on a host crash). Group commit lets many transactions share one fsync.

- **Undo log.** When InnoDB updates a row in place, it first copies the old version into the undo log. That serves two purposes: `ROLLBACK` restores the old version, and MVCC reads reconstruct what the row looked like for an older snapshot. A background purge thread deletes undo records once no snapshot needs them.

- **Doublewrite buffer.** To survive a torn page (a 16 KB page partially written when power dies), InnoDB writes each page first to a contiguous doublewrite area, then to its real home. On recovery, a half-written page is restored from the doublewrite copy.

### 3.5 MVCC, Oracle-style, via undo

Each clustered-index row carries hidden system columns:
- `DB_TRX_ID` (6 bytes), the last transaction to insert or update the row,
- `DB_ROLL_PTR` (7 bytes), a roll pointer to the undo record holding the previous version.

A transaction takes a read view (a snapshot of which transactions are committed). When it reads a row whose `DB_TRX_ID` is too new to be visible, InnoDB follows `DB_ROLL_PTR` back through the undo log to find a version it is allowed to see. This is the key contrast with PostgreSQL:

```
 PostgreSQL: UPDATE = write a NEW tuple; old version stays in the heap → bloat → VACUUM
 InnoDB:     UPDATE = modify row IN PLACE; old version goes to UNDO     → purge thread cleans undo
```

### 3.6 Locking and isolation

InnoDB does row-level locking on index records, with three flavours that together prevent phantoms:
- a **record lock** locks a single index record;
- a **gap lock** locks the gap between records (there is no row to lock, it just stops inserts into that range);
- a **next-key lock** is a record lock plus a gap lock on the preceding gap, and this is the default lock taken during an index scan under REPEATABLE READ.

There are also intention locks (table-level `IS`/`IX`, so the engine can check lock compatibility cheaply) and an insert-intention gap lock, so concurrent inserts into the same gap at different points do not block each other. InnoDB detects deadlocks automatically (using a wait-for graph) and rolls back the cheapest transaction.

Isolation levels:
| Level | Behaviour |
|---|---|
| READ UNCOMMITTED | dirty reads allowed |
| READ COMMITTED | fresh read view per statement; mostly record locks (more concurrency, more phantoms) |
| REPEATABLE READ (default) | one snapshot for the whole transaction; next-key locks prevent phantoms |
| SERIALIZABLE | strictest; plain SELECTs become locking reads |

Note that InnoDB's default is REPEATABLE READ, while PostgreSQL's is READ COMMITTED. Also, unlike the SQL standard, InnoDB's REPEATABLE READ avoids phantoms thanks to next-key locking.

---

## 4. Design Trade-Offs

**Why a clustered index?**
- PK lookups and PK range scans are extremely fast, because the row data is right there in the leaf, in order.
- There is no separate heap, the index is the table, so there is less duplication.
- Secondary indexes pay a double lookup (and they store the PK, so a fat PK bloats them all).
- Random-PK inserts (for example UUIDs) cause page splits and fragmentation, so auto-increment PKs are strongly preferred since inserts append to the rightmost page.

**Why both undo and redo?** They solve orthogonal problems. Redo guarantees durability: replay forward whatever committed but was not flushed. Undo enables atomicity/rollback and MVCC: reconstruct old versions. You cannot get both from one log, because one rolls forward and the other rolls back.

**InnoDB MVCC (in-place plus undo) vs PostgreSQL MVCC (append new tuples):**
| | InnoDB | PostgreSQL |
|---|---|---|
| Update | in place, old version to undo | write new tuple, old stays in heap |
| Cleanup | purge thread trims undo | VACUUM removes dead tuples |
| Cost of churn | undo grows; long transactions bloat undo / history list | heap and index bloat |
| Index impact | secondary index untouched if indexed cols unchanged | needs HOT to avoid index writes |

Neither is strictly better. InnoDB keeps the main table compact and clustered (great for read-heavy PK access) at the cost of undo management, while PostgreSQL keeps updates cheap and rollback trivial at the cost of VACUUM. (See [PostgreSQL_Internals](../PostgreSQL_Internals/README.md) for the other side.)

**Locking trade-off.** Next-key locking gives strong isolation (no phantoms at REPEATABLE READ), but it can lock more than the rows you touched, which occasionally causes surprising lock waits or deadlocks on range operations.

---

## 5. Experiments / Observations

These examples are illustrative, based on documented InnoDB behaviour, because MySQL was not installed locally.

**(a) Clustered vs covering vs double lookup.**

```sql
-- PK lookup → walks the clustered index directly (row is in the leaf)
EXPLAIN SELECT * FROM users WHERE id = 42;
   type: const   key: PRIMARY

-- Secondary lookup → uses email index, then back to clustered index (double lookup)
EXPLAIN SELECT * FROM users WHERE email = 'a@b.com';
   type: ref     key: idx_email     Extra: (row fetched via PK from clustered index)

-- Covering index → answered from the index alone, no clustered lookup
EXPLAIN SELECT email FROM users WHERE email = 'a@b.com';
   type: ref     key: idx_email     Extra: Using index
```

**(b) Watching MVCC and history grow.** A long-running transaction holds back purge, so undo accumulates, which shows up as a rising history list length:

```sql
SHOW ENGINE INNODB STATUS\G
   ...
   History list length 24531     -- grows while an old transaction stays open;
                                  -- drops once purge can advance
```

**(c) Next-key locking preventing a phantom (REPEATABLE READ).**

```sql
-- session 1
START TRANSACTION;
SELECT * FROM orders WHERE amount > 100 FOR UPDATE;   -- next-key locks the range
-- session 2
INSERT INTO orders(amount) VALUES (150);              -- BLOCKS: gap is locked
                                                      -- → no phantom row appears in session 1
```

Observation: in each case the behaviour traces straight back to the architecture. The clustered index makes PK access cheap but secondary access a double hop, undo-based MVCC means old transactions inflate undo rather than the table, and next-key gap locks are what make REPEATABLE READ phantom-free.

---

## 6. Key Learnings

1. **The table is an index.** InnoDB's clustered B+tree means the primary key and the table's physical storage are the same thing. This single fact explains fast PK access, the secondary-index double lookup, and why short auto-increment PKs matter.
2. **Two logs, two jobs.** Redo rolls forward for durability and recovery, undo rolls back for atomicity and to serve old versions for MVCC. Asking "why both?" is the wrong question, because they are opposites.
3. **InnoDB and PostgreSQL took opposite MVCC routes.** In-place update plus undo (compact table, purge thread) vs a new tuple per update plus VACUUM (cheap updates, bloat). Same goal (readers do not block writers), opposite mechanics, opposite cleanup costs.
4. **Locking is finer-grained than it looks.** Beyond plain row locks, gap and next-key locks lock ranges, which is how InnoDB gives phantom-free REPEATABLE READ as its default, at the cost of occasionally locking more than expected.
5. **A lot of the cleverness is about I/O.** The buffer pool's midpoint insertion makes it scan-resistant, the change buffer batches secondary-index writes, and the doublewrite buffer trades extra writes for torn-page safety. The recurring theme is trading memory, CPU, or extra writes to avoid slow random disk I/O.

---

## References

- MySQL 8.4 Reference Manual: [InnoDB Index Types](https://dev.mysql.com/doc/refman/8.4/en/innodb-index-types.html), [InnoDB Multi-Versioning](https://dev.mysql.com/doc/refman/8.4/en/innodb-multi-versioning.html), [Buffer Pool / Midpoint Insertion](https://dev.mysql.com/doc/refman/8.4/en/innodb-buffer-pool.html), [Redo Log](https://dev.mysql.com/doc/refman/8.4/en/innodb-redo-log.html), [InnoDB Locking](https://dev.mysql.com/doc/refman/8.4/en/innodb-locking.html), [Transaction Isolation Levels](https://dev.mysql.com/doc/refman/8.4/en/innodb-transaction-isolation-levels.html), [Doublewrite Buffer](https://dev.mysql.com/doc/refman/8.4/en/innodb-doublewrite-buffer.html)
- Jeremy Cole: [The physical structure of InnoDB index pages](https://blog.jcole.us/2013/01/07/the-physical-structure-of-innodb-index-pages/)
