# MySQL / InnoDB Storage Engine

> Advanced DBMS — System Design Discussion  
> Roll No: 10075 | Nase Anishka

---

## 1. Problem Background

MySQL's old default storage engine, MyISAM, had a serious problem: no transactions, no crash recovery, and table-level locking. If a write failed halfway, the table could be left in a broken state. Two users writing to the same table at the same time would block each other — even if they were touching completely different rows.

InnoDB was built to fix this. It gave MySQL what MyISAM couldn't:
- Real ACID transactions — commit means committed, rollback means gone
- Crash recovery — data survives a hard power loss if the transaction was committed
- Row-level locking — two writes to different rows don't block each other
- MVCC — reads don't block writes

MySQL made InnoDB the default in version 5.5 (2010). Today InnoDB is what almost everyone means when they say "MySQL."

What makes InnoDB particularly interesting to study is that its approach to MVCC is fundamentally different from PostgreSQL's. PostgreSQL writes a new copy of a row on every update. InnoDB updates rows in-place and keeps old versions in a separate undo log. Same goal, completely different mechanism — and it leads to different trade-offs in performance, cleanup, and storage.

---

## 2. Architecture Overview

```
  MySQL SQL Layer (Parser → Optimizer → Executor)
             |
             | handler API
             v
  +------------------------------------------+
  |          InnoDB Storage Engine            |
  |                                           |
  |  IN MEMORY:                               |
  |  +---------+  +-----------+  +---------+  |
  |  | Buffer  |  | Log       |  | Change  |  |
  |  | Pool    |  | Buffer    |  | Buffer  |  |
  |  | (16KB   |  | (WAL in   |  | (deferred|
  |  | pages)  |  | RAM)      |  | sec-idx) |  |
  |  +---------+  +-----------+  +---------+  |
  |       |              |                    |
  |       v              v                    |
  |  Tablespace      Redo Log               |
  |  (.ibd file)     (#innodb_redo/)         |
  |  - clustered idx                         |
  |  - secondary idx    Undo Logs            |
  |                     (rollback segments)  |
  |                                          |
  |                     Doublewrite Buffer   |
  |                     (torn-page safety)   |
  +------------------------------------------+
```

The SQL layer (MySQL's parser and optimizer) passes queries to InnoDB through a "handler" API. InnoDB reads and writes data through the buffer pool. Before any change hits the data file, it goes to the redo log (WAL). Before a row is updated in-place, the old version goes to the undo log. Dirty pages are eventually flushed through the doublewrite buffer to avoid torn-page corruption.

---

## 3. Internal Design

### The Clustered Index — tables stored as B+trees

The most important thing to know about InnoDB: **every table is stored as a B+tree sorted by its primary key, and the full row lives in the leaf nodes**. There's no separate heap file like in PostgreSQL. The table and its primary index are the same structure.

```
  Clustered Index (PK = id):

        [50 | 200]               ← internal nodes: separator keys
       /     |     \
  [1..49] [50..199] [200..]      ← leaf nodes: contain the FULL row
  | id=1  |  id=50  |
  | name=A|  name=B |
  | ...   |  ...    |
```

InnoDB picks the clustering key in this order:
1. Your explicit `PRIMARY KEY`
2. The first `UNIQUE NOT NULL` index if no PK exists
3. A hidden internal row ID if neither exists

This design makes **primary key lookups and range scans very fast** — a range scan like `WHERE id BETWEEN 100 AND 200` reads physically adjacent leaf pages. But it has a downside: if you use random primary keys (like UUIDs), inserts scatter across the tree and cause constant page splits and fragmentation. That's why InnoDB best practice is to use auto-increment integer primary keys.

### Secondary Indexes and the Double Lookup

Secondary indexes in InnoDB are separate B+trees. But their leaf nodes don't store row pointers — they store the primary key value:

```
  Secondary index on (email):
  Leaf: (email='x@y.com', pk=42)

  To get the full row:
  Step 1: walk the email B+tree → get pk=42
  Step 2: walk the clustered index → get the full row
```

This two-step lookup is the cost of every secondary index query. The only way to avoid step 2 is a **covering index** — if all columns the query needs are already in the secondary index, InnoDB answers directly from it (shown as `Using index` in EXPLAIN).

One side effect: the primary key value is stored in every secondary index. A UUID primary key (36 bytes) bloats all secondary indexes because it's duplicated in each one.

### Buffer Pool

The buffer pool is InnoDB's in-memory page cache. It holds 16KB pages and is usually the single most important thing to tune for performance (`innodb_buffer_pool_size`).

Its LRU eviction has a twist designed to protect against full table scans evicting hot data:

```
  LRU list:
  [ ── hot/young (5/8 of pool) ── | ── old (3/8) ── ]
                                   ^ midpoint

  New pages land at the MIDPOINT (head of the old section).
  They only get promoted to hot if accessed again after 1 second.
```

A full table scan reads many pages once — they land in the old section and age out quickly. Pages that are accessed repeatedly make it to the hot section and stay there. This prevents a one-off scan from wiping out your working set.

InnoDB also has an **Adaptive Hash Index (AHI)** — it automatically builds an in-memory hash table for the most frequently-accessed primary key lookups. When a certain PK is accessed repeatedly, the AHI lets InnoDB answer it with a single hash lookup instead of walking the B+tree from root to leaf. It's automatic and transparent.

### Redo Log and Undo Log

InnoDB has two separate logs. They're often confused but they do completely opposite things:

| | Redo Log | Undo Log |
|--|--|--|
| Purpose | Crash recovery | Rollback + MVCC old versions |
| Direction | Roll forward | Roll back |
| Where stored | `#innodb_redo/` | Rollback segments (undo tablespace) |
| Cleaned by | Checkpoint advances | Purge thread |

**Redo log:** before a dirty page is written to disk, its change must already be in the redo log (WAL). On crash, InnoDB replays redo records from the last checkpoint to recover committed changes. At COMMIT, the log buffer is flushed to disk. The setting `innodb_flush_log_at_trx_commit=1` (default) fsyncs on every commit — full durability. Setting it to `2` fsyncs about once per second — faster but you can lose up to 1 second on a crash.

**Undo log:** before updating a row in-place, InnoDB copies the old column values to the undo log. This serves two purposes: ROLLBACK (re-apply the old values) and MVCC (let other transactions read the old version). The undo log is a chain of old versions, traversed backwards.

### MVCC via Undo Chains

Each row in the clustered index has two hidden fields:
- `DB_TRX_ID` — the transaction that last modified this row
- `DB_ROLL_PTR` — a pointer to the undo record containing the previous version

When a transaction reads a row that's "too new" for its snapshot, InnoDB follows `DB_ROLL_PTR` backward through the undo chain until it finds a version it's allowed to see:

```
  Current row: name='Charlie' (modified by txn 50)
       |
       DB_ROLL_PTR
       v
  Undo record: name='Bob' (modified by txn 30)
       |
       prev ptr
       v
  Undo record: name='Alice' (original, txn 10)
```

A transaction with a snapshot that only knows about txns up to 35 would see `name='Bob'`.

This is the key difference from PostgreSQL: in PostgreSQL, old versions live in the heap file. In InnoDB, old versions live in the undo log. In both cases, a long-running transaction that holds onto an old snapshot prevents cleanup — but in InnoDB the symptom is undo log growth (visible as a rising "history list length"), not heap file bloat.

### Locking

InnoDB locks at the index-record level, not the row level directly. Three types of locks build on each other:

- **Record lock**: locks a specific index entry
- **Gap lock**: locks the space *between* index entries (no actual row — just prevents inserts into that range)
- **Next-key lock**: record lock + the gap before it — this is the default under REPEATABLE READ

Next-key locks are why InnoDB's default isolation level (REPEATABLE READ) prevents phantom reads. If you scan `WHERE amount > 100`, InnoDB locks those records and the gaps between them. A concurrent INSERT of `amount=150` blocks because the gap is locked.

Deadlock detection is automatic — InnoDB maintains a waits-for graph and rolls back the cheapest transaction when it finds a cycle.

---

## 4. Design Trade-Offs

**Clustered index is great for PK access, painful with random keys.**  
A PK lookup goes straight to the row in one B+tree traversal — no heap fetch needed. A PK range scan reads physically adjacent pages. These are real wins. But UUID primary keys destroy this: every insert goes to a random location in the tree, causing splits everywhere. Auto-increment is strongly preferred.

**Double lookup for secondary indexes.**  
Every secondary index query that needs columns not in the index does two B+tree traversals. PostgreSQL has the same "two lookups" issue (index → heap file). InnoDB's version might be slightly faster because the clustered index is predictably organized, but it's the same fundamental cost. Covering indexes are the solution in both databases.

**Why do you need both undo and redo logs?**  
They solve different problems. Redo is about durability — replay committed changes that didn't make it to the data file before a crash. Undo is about atomicity and old versions — roll back uncommitted changes and let MVCC readers see old state. One rolls forward, the other rolls back. You can't do both with one log.

**InnoDB MVCC vs PostgreSQL MVCC.**  
InnoDB updates rows in-place and keeps old versions in the undo tablespace. PostgreSQL writes new row versions into the heap and keeps old ones there too. InnoDB's approach keeps the main data file compact and doesn't need the heap-bloat VACUUM that PostgreSQL does. But InnoDB's undo tablespace can grow large under long transactions. Different storage location, same fundamental problem when transactions stay open too long.

**Next-key locking is protective but occasionally over-locks.**  
Preventing phantom reads at REPEATABLE READ is a genuinely stronger guarantee than most databases offer at that level. The downside is that range queries lock gaps, which can block concurrent inserts that don't conflict on any actual row. This causes surprising lock waits in some workloads.

---

## 5. Experiments / Observations

These are based on documented MySQL 8.0/8.4 behavior — MySQL wasn't installed locally, but the behavior described is from the official reference manual.

**Clustered vs. covering index in EXPLAIN:**
```sql
-- PK lookup, one tree, row in leaf
EXPLAIN SELECT * FROM users WHERE id = 42;
-- type: const, key: PRIMARY

-- Secondary index, two trees
EXPLAIN SELECT * FROM users WHERE email = 'x@y.com';
-- type: ref, key: idx_email (row fetched from clustered index too)

-- Covering index, one tree, no clustered lookup
EXPLAIN SELECT email FROM users WHERE email = 'x@y.com';
-- type: ref, key: idx_email, Extra: Using index
```

The `Using index` in the third case confirms no clustered-index lookup happened.

**History list length — undo health check:**
```sql
SHOW ENGINE INNODB STATUS\G
-- History list length 2314
```

This number should stay near zero. A rising history list means the purge thread can't clean old undo records because some transaction is still holding an old snapshot open. A long-running transaction (like an idle `BEGIN` left open) can cause this to grow rapidly.

**Next-key lock blocking an insert:**
```sql
-- Session 1:
START TRANSACTION;
SELECT * FROM orders WHERE amount > 100 FOR UPDATE;
-- Locks all index records with amount > 100 and the gaps

-- Session 2:
INSERT INTO orders(amount) VALUES (150);
-- BLOCKED — the gap covering 150 is locked
```

This shows exactly how next-key locks prevent phantoms.

**Adaptive Hash Index in INNODB STATUS:**  
After accessing the same primary key in a loop, `SHOW ENGINE INNODB STATUS` shows the AHI stats — hash table size, used cells, and searches/s. The hot key gets cached in the hash table and subsequent lookups skip the B+tree traversal entirely.

---

## 6. Key Learnings

1. **The clustered index is the defining feature of InnoDB.** Storing the full row in the B+tree leaf means primary key access is always one traversal with no separate heap fetch. Everything else — why secondary indexes cost two lookups, why short PKs matter, why UUID keys are slow — comes from this one decision.

2. **Redo and undo are not redundant.** Redo rolls forward for crash recovery. Undo rolls back for atomicity and MVCC. They do opposite things. You need both because no one log can do both jobs.

3. **MVCC cleanup in InnoDB happens in the undo tablespace, not the heap.** Old row versions live in undo records. The purge thread cleans them. Monitoring history list length is how you know if purge is keeping up. This is the equivalent of watching dead tuple count and VACUUM in PostgreSQL.

4. **Next-key locking gives stronger isolation than the SQL standard requires.** Most databases allow phantoms at REPEATABLE READ. InnoDB's gap locks prevent them, which is a good thing — but occasionally causes unexpected lock contention on range queries.

5. **Buffer pool management matters as much as data structure design.** The midpoint insertion LRU, the Adaptive Hash Index, and the change buffer are all there to protect the buffer pool's effectiveness — from scan pollution, from hot-key B+tree overhead, and from random secondary index I/O. The buffer pool hit rate is often the dominant factor in InnoDB performance.

---

## References

- MySQL 8.4 Reference Manual: [InnoDB Architecture](https://dev.mysql.com/doc/refman/8.4/en/innodb-architecture.html), [Clustered Indexes](https://dev.mysql.com/doc/refman/8.4/en/innodb-index-types.html), [MVCC](https://dev.mysql.com/doc/refman/8.4/en/innodb-multi-versioning.html), [Buffer Pool](https://dev.mysql.com/doc/refman/8.4/en/innodb-buffer-pool.html), [Redo Log](https://dev.mysql.com/doc/refman/8.4/en/innodb-redo-log.html), [Locking](https://dev.mysql.com/doc/refman/8.4/en/innodb-locking.html)
- Jeremy Cole: [Physical structure of InnoDB index pages](https://blog.jcole.us/2013/01/07/the-physical-structure-of-innodb-index-pages/)
