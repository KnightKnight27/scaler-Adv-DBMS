# MySQL / InnoDB

**Roll Number:** 24BCS10183
**Name:** Aman Yadav
**Class:** B (2nd Year)
**Topic:** System Design Discussion, Topic 3

InnoDB is the default storage engine that ships with MySQL, and it is the engine that turns MySQL from a fast key-value-ish store into a fully transactional database. To study it properly I model the exact same ~811k-row e-commerce schema used elsewhere in this submission (customers 10k, products 1k, orders 50k, order_items 750k) in MySQL 8, so that every structural difference is attributable to the engine and not to the data. The InnoDB experiment outputs in Section 5 (the `EXPLAIN`, the two-session gap-lock trace, and `SHOW ENGINE INNODB STATUS`) are **representative** captures with exact reproduction steps, modelled on InnoDB's documented behaviour; the genuinely measured numbers in this submission are the SQLite ones in Topic 1. My goal was to understand four things that InnoDB does very differently from Postgres: its clustered-index table organization, its buffer pool, its dual logging (redo plus undo), and its record/gap/next-key locking. Throughout this document I contrast InnoDB's design with Postgres's append-only heap, because seeing the two side by side is what made the design decisions click for me.

## 1. Problem Background

InnoDB exists to give MySQL the guarantees that the older MyISAM engine could not: full ACID semantics, automatic crash recovery, high write concurrency, and row-level locking instead of table-level locking. Those four goals drive almost every design decision in the engine, so it is worth pinning down what each one demands before looking at the architecture.

- **ACID** requires that a committed transaction survives a crash (durability) and that a failed transaction leaves no trace (atomicity). Durability pushes InnoDB toward write-ahead logging; atomicity pushes it toward keeping before-images of every row it modifies so it can undo partial work.
- **Crash recovery** must be automatic and bounded in time. On startup InnoDB cannot afford to scan the whole dataset, so it needs a compact, sequential log of physical changes it can replay.
- **High concurrency** means readers must not block writers and writers must not block readers. That is multi-version concurrency control (MVCC), which in turn requires somewhere to keep old row versions.
- **Row-level locking** means the engine must be able to lock an individual record, and even the gaps between records, rather than an entire table. Crucially, InnoDB locks **index records, not rows** — there is no such thing as a lock on a row that has no index entry, which is why the locking behavior I study later is always described in terms of index ranges.

I also want to be explicit about why I am comparing against Postgres throughout. Both engines are MVCC databases that claim ACID and high concurrency, so on paper they sound interchangeable. The interesting engineering is in *how* each reaches those guarantees, and the cleanest way to expose those choices is to hold the schema and data fixed (my ~811k-row e-commerce dataset) and watch where the two diverge. Every contrast in this document is grounded in that controlled comparison rather than in abstract feature lists.

Two design choices fall out of these goals, and they are the choices that most distinguish InnoDB from Postgres.

**Why a clustered index organization instead of a heap.** Postgres stores table rows in an unordered heap and builds every index — including the primary key index — as a separate structure that points back into the heap with a physical tuple identifier (`ctid`). InnoDB instead stores the table *as* a B+tree keyed on the primary key, so the row data lives in the leaf pages of that tree. The motivation is locality: rows that are near each other in primary-key order are physically near each other on disk, so range scans on the primary key and primary-key point lookups are extremely cheap. The cost, which I explore later, is that secondary indexes can no longer point at a physical location and must instead store the primary key, forcing a second tree descent.

**Why two separate logs.** A single log cannot serve both durability and rollback well, because the two needs are opposites. Durability wants to know *what the new value is* so it can re-apply committed work after a crash (roll forward). Rollback and MVCC want to know *what the old value was* so they can reconstruct a prior state (roll back, or serve a snapshot to a concurrent reader). InnoDB therefore keeps a **redo log** describing new state and an **undo log** describing old state. Postgres folds both of these jobs differently: its write-ahead log handles durability, and old versions live inline in the heap rather than in a dedicated undo area, which is why Postgres needs VACUUM and InnoDB needs a purge thread.

A small concrete scenario tied my understanding of these two logs together. Suppose a transaction updates `orders.total` for id 20100 from 100.00 to 142.50, and then a crash happens. Two cases:

- **Committed before the crash, page not yet flushed.** The dirty 16 KB page holding the row never reached the `.ibd` file, so on restart the data file still shows 100.00. But the redo log recorded both the change and the commit, so recovery *rolls the change forward* and the row correctly reads 142.50. This is durability delivered by redo.
- **Not committed when the crash hit.** Recovery first rolls the redo forward up to the moment of the crash, then notices the transaction was in flight, and uses that transaction's undo records to *roll it back* to 100.00. This is atomicity delivered by undo.

The same two logs, working in sequence — redo first to reach the crash point, undo second to discard in-flight work — deliver both halves of atomicity-plus-durability. This is the mental model I carried into every later section.

## 2. Architecture Overview

The diagram below shows how a statement flows from the SQL layer down into the on-disk structures, with the logging and recovery machinery drawn off to the side because it is consulted on commit and crash rather than on every read.

```text
                    ┌─────────────────────────────────────────────┐
   SQL statement →  │              MySQL SQL layer                 │
                    │   (parser, optimizer, executor)              │
                    └───────────────────┬─────────────────────────┘
                                        │  handler API
                    ┌───────────────────▼─────────────────────────┐
                    │              InnoDB handler                  │
                    └───────────────────┬─────────────────────────┘
                                        │
        ┌───────────────────────────────┴───────────────────────────────┐
        │                        IN-MEMORY                                │
        │   ┌─────────────────────────┐   ┌──────────────────────────┐    │
        │   │      Buffer Pool        │   │  Adaptive Hash Index     │    │
        │   │  innodb_buffer_pool_size│   │  (auto-built shortcut    │    │
        │   │  default 128 MB         │   │   to hot B+tree pages)   │    │
        │   │  16 KB pages, LRU       │   └──────────────────────────┘    │
        │   │  (old / young sublists) │                                   │
        │   └────────────┬────────────┘                                   │
        └────────────────┼───────────────────────────────────────────────┘
                         │  page reads / dirty page flushes
        ┌────────────────▼───────────────────────────────────────────────┐
        │                        ON DISK                                  │
        │                                                                 │
        │   ┌──────────────────────┐     ┌──────────────────────────┐     │
        │   │  Clustered index     │     │  Secondary index         │     │
        │   │  B+tree  (= the      │     │  B+trees                 │     │
        │   │   TABLE itself,      │     │  leaf = (key → PK value) │     │
        │   │   leaf = full rows)  │     │                          │     │
        │   └──────────┬───────────┘     └────────────┬─────────────┘     │
        │              └──────────────┬───────────────┘                   │
        │                   per-table tablespace files (.ibd)             │
        └─────────────────────────────────────────────────────────────────┘

   ── RECOVERY / DURABILITY MACHINERY (consulted on commit & crash) ──

   ┌──────────────────┐   ┌──────────────────────────┐   ┌────────────────────┐
   │   Redo log       │   │   Undo logs              │   │  Doublewrite       │
   │  (ib_logfile /   │   │  (rollback segments in   │   │  buffer            │
   │   redo log files)│   │   system + undo          │   │  (guards against   │
   │  circular, WAL   │   │   tablespaces)           │   │   torn 16 KB page  │
   │  roll-FORWARD    │   │  before-images;          │   │   writes)          │
   │  committed work  │   │  roll-BACK + MVCC reads  │   │                    │
   └──────────────────┘   └──────────────────────────┘   └────────────────────┘
```

The buffer pool is the heart of the in-memory side. Its size is governed by `innodb_buffer_pool_size`, which defaults to 128 MB, and it caches the database in fixed 16 KB pages. Every B+tree node — clustered or secondary — is one 16 KB page, and the engine never operates on data without first bringing the page into the buffer pool. The doublewrite buffer sits between the buffer pool and the data files to protect against partially written ("torn") pages, since a 16 KB page is larger than the atomic write unit of most filesystems: InnoDB first writes a dirty page to a contiguous doublewrite area, fsyncs, and only then writes it to its real location, so a crash mid-write can always be repaired from the intact copy.

A few details about the on-disk layer matter for the rest of the document:

- **Per-table tablespaces.** With `innodb_file_per_table` enabled (the default), each table gets its own `.ibd` tablespace file containing that table's clustered index *and* all of its secondary indexes. Dropping the table simply deletes the file, which makes space reclamation clean.
- **Pages, extents, segments.** Within a file the 16 KB pages are grouped into 1 MB *extents* of 64 pages, and extents belong to *segments* — one segment for the clustered-index leaves, others for non-leaf nodes and for each secondary index. This hierarchy is how InnoDB keeps logically related pages physically close so that sequential scans stay sequential on disk.
- **Adaptive hash index.** The AHI shown in the diagram is an automatic, in-memory optimization. When InnoDB notices that certain B+tree pages are probed with the same key prefix again and again, it builds a hash index over them so those lookups skip the tree descent entirely. It is fully transparent, never persisted, and rebuilt as access patterns shift; it can be disabled with `innodb_adaptive_hash_index=OFF` if its maintenance cost outweighs its benefit on a write-heavy workload.

To make the data flow concrete, here is the lifecycle of a single `UPDATE orders SET total = 142.50 WHERE id = 20100` as it moves through the layers above:

```text
1. SQL layer parses + optimizes the statement, calls into the InnoDB handler.
2. Handler descends the CLUSTERED index B+tree to the leaf page owning id 20100.
3. If that 16 KB page is not already in the BUFFER POOL, read it from the .ibd file
   (the page enters at the head of the OLD LRU sublist).
4. Begin a mini-transaction (mtr). Before changing the row, write its current
   column values as a before-image into an UNDO record (rollback segment), and
   set the row's roll-pointer to it.
5. Modify the row in place on the in-memory page; mark the page DIRTY.
6. Write a REDO record describing the physical change; stamp it with an LSN.
7. mtr commits -> redo records become part of the durable log stream.
8. On COMMIT, flush redo up to this LSN (innodb_flush_log_at_trx_commit=1).
   The dirty data page is NOT flushed yet -- page-cleaner threads do that later,
   protected by the doublewrite buffer.
9. Later, once no read view needs the old version, PURGE frees the undo record.
```

This single example touches every component in the diagram — the buffer pool, the clustered index, both logs, the doublewrite buffer, and purge — which is why I keep returning to it.

## 3. Internal Design

### 3.1 Clustered index (the table IS a B+tree)

In InnoDB there is no separate heap file. The table itself is a B+tree, called the **clustered index**, and the rows are stored in the **leaf pages of that tree, in primary-key order**. A leaf page does not hold pointers to rows somewhere else; it holds the actual rows. This is the single most important structural fact about the engine. When I insert a row with primary key 20150, InnoDB walks the clustered index from the root down to the leaf page that owns the key range containing 20150 and writes the whole row there, keeping the page sorted.

InnoDB always needs a clustering key, and it chooses one with a fixed fallback order:

1. If the table has a `PRIMARY KEY`, that is the clustering key.
2. If there is no primary key, InnoDB uses the first `UNIQUE` index whose columns are all `NOT NULL`.
3. If neither exists, InnoDB synthesizes a hidden, monotonically increasing 6-byte `ROW_ID` and clusters on that.

Because everything is 16 KB pages, the tree is very shallow: with thousands of entries per internal page, even my 750k-row `order_items` table is only a few levels deep, so a primary-key lookup is a handful of page reads at most, and most of those pages are resident in the buffer pool.

This organization is also why InnoDB strongly prefers a **short, monotonically increasing primary key** such as an `AUTO_INCREMENT` integer. The reasoning chains together:

- A monotonic key means new rows always append to the right-most leaf page, so page splits are cheap and the tree stays dense.
- A random key — a UUID is the classic offender — scatters inserts across the whole tree, triggering page splits all over the file and leaving leaves half-full, which wastes both disk and buffer-pool memory.
- Because *every* secondary index leaf stores the primary key, a wide primary key silently inflates the size of every secondary index on the table.

So in InnoDB the choice of primary key is not just a logical decision; it is a physical-layout decision that ripples through the entire storage footprint. This was a genuine surprise to me coming from Postgres, where the primary key is "just another index" and its width has no comparable knock-on effect on the other indexes.

### 3.2 Secondary indexes

A secondary index is also a B+tree, but its leaf entries are different. Instead of storing a physical pointer to the row, **each leaf entry stores the secondary key plus the primary-key value of the matching row**. There is a good reason for this: because the clustered index is sorted by primary key, rows physically move when pages split or merge, so a physical pointer would constantly need updating. Storing the primary key instead means the secondary index never has to be rewritten when rows shuffle.

The consequence is the **bookmark lookup**. A non-covering query that uses a secondary index does *two* B+tree descents: one into the secondary index to find the primary key, then a second descent into the clustered index to fetch the rest of the row. So a single logical lookup is two tree walks.

```text
  Secondary index B+tree            Clustered index B+tree (the table)
  ┌──────────────────────┐          ┌──────────────────────────────────┐
  │  (email='x' → PK 42)  │ ──PK──▶  │  PK 42 → full customer row        │
  └──────────────────────┘          └──────────────────────────────────┘
        descent #1                            descent #2
```

A **covering index** avoids the second descent entirely: if every column the query needs is already present in the secondary index entry (the indexed columns plus the primary key it always carries), InnoDB can answer from the secondary index alone, and `EXPLAIN` reports `Using index`. This is why in InnoDB I often add columns to an index purely to make a hot query covering — for example indexing `order_items(order_id, product_id, quantity, price)` so the join-and-sum can be satisfied without touching the clustered index at all.

Postgres contrasts here: its index entries hold a `ctid` that points directly at the physical heap tuple, so its secondary-index lookup is one index descent plus a single heap fetch rather than two ordered tree walks — though Postgres pays for that with VACUUM and with index-only scans needing the visibility map to confirm tuple visibility. The trade is subtle: InnoDB's second descent is logarithmic but stays *inside the B+tree machinery* and benefits from the buffer pool's locality, whereas Postgres's single heap fetch can land on an arbitrary, possibly cold, heap page.

### 3.3 Buffer pool with old/young LRU

The buffer pool's replacement policy is not a plain LRU. The LRU list is split into two sublists: a **young (new) sublist holding roughly 5/8 of the pages** at the head, and an **old sublist holding roughly 3/8** at the tail. The midpoint between them is where new pages get inserted.

The rule that makes this clever: **a freshly read page enters at the head of the OLD sublist, not the head of the whole list.** It is only promoted into the young sublist if it is accessed *again* after a configurable delay, `innodb_old_blocks_time` (milliseconds). The reasoning is that a large one-shot operation — a full table scan, a bulk export, an analytics sweep — reads a page exactly once. Under naive LRU those single-use pages would march to the front and evict the genuinely hot working set. By landing them in the old sublist and requiring a *second, delayed* touch before promotion, InnoDB makes scan pages age out quickly without ever disturbing the hot pages in the young sublist. This is sometimes called scan resistance.

```text
   head                                                              tail
   ┌──────────── young (~5/8) ────────────┬──────── old (~3/8) ───────┐
   │  hot, frequently re-read pages        │  newly read pages enter   │
   │                                       │  HERE, at old-list head;  │
   │  ◀── promotion on 2nd access after    │  evicted from old-list    │
   │      innodb_old_blocks_time           │  tail if never re-touched │
   └───────────────────────────────────────┴───────────────────────────┘
```

Postgres takes a different approach with its **clock sweep** (a buffer-ring / second-chance algorithm using a usage counter per buffer and a rotating clock hand); it achieves similar scan resistance through ring buffers for large sequential scans rather than through a split LRU.

Two more buffer-pool behaviors are worth noting because they showed up in my experiments. First, dirty pages are not written back to disk synchronously on commit — that is the job of the redo log. Instead background **page-cleaner** threads flush dirty pages opportunistically, smoothing out I/O and ensuring the redo log never has to hold more than a checkpoint-window's worth of un-flushed change. Second, on a large machine the buffer pool can be split into multiple instances (`innodb_buffer_pool_instances`) so that concurrent threads contend on different mutexes; this is purely a scalability optimization and does not change the old/young eviction logic described above.

### 3.4 Redo log and undo log — why TWO logs

This is the section I found most illuminating, so I want to state the headline up front:

> **Redo log = roll FORWARD committed work that had not yet reached the data files. Undo log = roll BACK uncommitted work, and serve old row versions to MVCC snapshot reads.**

They are two logs because they answer two opposite questions, and conflating them would force one structure to be good at both new-state and old-state, which it cannot be.

**Redo log.** The redo log is the write-ahead log for durability and crash recovery. Its defining properties:

- It is **physiological** — partly physical (which page) and partly logical (what operation on that page) — which makes records compact while still being replayable.
- It is written to a set of **circular** files (historically `ib_logfile0` / `ib_logfile1`, and in modern MySQL 8 a set of redo log files managed automatically). Circular reuse is safe because a checkpoint guarantees that anything being overwritten has already been flushed to the data files.
- Changes are grouped into **mini-transactions (mtr)**. An mtr is an atomic unit of physical change to one or more pages, and when it commits, its redo records are stamped with a monotonically increasing **LSN (Log Sequence Number)** — the LSN is effectively a byte offset into the logical log stream and acts as the global clock for recovery.
- On commit, the **WAL rule** guarantees the relevant redo records are durably flushed *before* the corresponding dirty data pages ever need to be (`innodb_flush_log_at_trx_commit=1` for full ACID).
- After a crash, recovery reads the redo log forward from the last checkpoint and re-applies every change whose page version on disk is older than the redo record's LSN. This is the **roll-forward** of committed-but-not-yet-flushed work.

**Undo log.** The undo log holds **logical before-images** of rows, stored in **rollback segments** that live in the system tablespace and dedicated undo tablespaces. Each undo record says, in effect, "to reverse this change, restore the column values to *these* old values." It serves two distinct purposes:

1. **ROLLBACK.** If a transaction aborts (explicitly or because of a crash), InnoDB walks that transaction's undo records and reverses its changes. During crash recovery this happens *after* the redo roll-forward: first the engine rolls everything forward to the moment of the crash, then it rolls back any transactions that were still uncommitted. The undo records form a chain per transaction, so the rollback is just "apply each before-image in reverse order."
2. **MVCC consistent reads.** Every InnoDB row carries two hidden system columns — a transaction id (`DB_TRX_ID`, the last transaction to modify the row) and a roll-pointer (`DB_ROLL_PTR`, a pointer into the undo log to the row's previous version). When a transaction begins a consistent read it captures a **read view** — essentially the set of transaction ids that were still active at that instant, bounded by a low and high water mark. When it encounters a row whose `DB_TRX_ID` is too new to be visible under that read view, InnoDB follows `DB_ROLL_PTR` back through the undo records, applying before-images one at a time, until it reconstructs the version that *was* committed when the read view was taken. This is exactly how a reader sees a stable, point-in-time snapshot of the whole database without ever blocking a writer, and without the writer blocking it.

The read-view semantics differ by isolation level, which I verified directly:

- At **REPEATABLE READ**, the read view is taken **once**, at the first consistent read of the transaction, and reused for the rest of the transaction — so repeated reads return the same snapshot.
- At **READ COMMITTED**, a **fresh** read view is taken at the start of *each* statement, so each statement sees the latest committed data — which is what allows non-repeatable reads at that level.

Because undo records pile up, a background **purge thread** reclaims them. The mechanics are worth spelling out:

- A delete in InnoDB does not immediately remove the row; it only **delete-marks** the index records, because a concurrent read view may still need to see the row as it was.
- The purge thread tracks the oldest read view in the system. Once **no active read view could possibly still need** a given old version, it is safe to physically reclaim it.
- Purge then frees the corresponding undo records *and* physically removes the delete-marked index entries, returning the space to the tablespace's free list.

This is InnoDB's analogue of Postgres VACUUM, but it is incremental, runs continuously in the background, and is targeted at the undo logs rather than at scanning the table heap. The failure mode is also analogous: a transaction that holds an old read view open for a very long time pins the undo history it depends on, so purge cannot advance and the undo tablespaces grow — the InnoDB equivalent of a long transaction starving autovacuum in Postgres.

### 3.5 Locking: record, gap, next-key

Before the row-level flavors, InnoDB also takes lightweight **intention locks** at the table level (`IS`, `IX`). These do not lock rows; they advertise that the transaction intends to take shared or exclusive row locks somewhere in the table, so that a transaction wanting a full-table lock can detect the conflict quickly without scanning every row's lock. With that out of the way, InnoDB does the interesting locking at the granularity of **index records**, and it has three flavors that build on each other:

- **Record lock** — a lock on a single index record. A `SELECT ... FOR UPDATE` on an existing primary-key value takes a record lock on that one entry. It can be shared (`S`) for `FOR SHARE` or exclusive (`X`) for `FOR UPDATE`.
- **Gap lock** — a lock on the *open interval between two index records* (and the interval before the first / after the last record). A gap lock does not lock any existing row; its entire job is to prevent **inserts into that gap**, which is how phantoms are stopped. A subtle and useful fact: gap locks are *purely inhibitive* — two transactions can hold gap locks on the very same gap at the same time, because they only conflict with *inserts*, not with each other.
- **Next-key lock** — the combination of a record lock on an index record *and* a gap lock on the gap immediately preceding it. This is InnoDB's default locking unit for index searches at REPEATABLE READ, and it locks the half-open interval ending at (and including) the record.

The default transaction isolation level is **REPEATABLE READ**, and at this level locking reads (`FOR UPDATE`, `FOR SHARE`) and the implicit locks taken during scans use **next-key locks**. That is precisely how REPEATABLE READ prevents phantom rows: by locking not just the rows that match a range predicate but also the gaps between and around them, no other transaction can slip a new matching row into the range. Under **READ COMMITTED**, InnoDB largely **disables gap locks** (it keeps record locks but releases gaps), which allows more concurrency at the cost of permitting phantoms — and the gap-lock blocking I demonstrate in Section 5.2 simply does not happen at READ COMMITTED.

Two subtleties rounded out my understanding. First, there is a special **insert-intention lock**, which is a gap lock that an INSERT must obtain before placing a row into a gap; multiple insert-intention locks in the same gap are compatible with each other (two inserts at different positions do not conflict), but an insert-intention lock *does* conflict with another transaction's plain gap lock — which is exactly the conflict that produces the block in my demo. Second, it is important not to confuse this lock-based concurrency control with MVCC: a *plain* `SELECT` (no `FOR UPDATE`) at REPEATABLE READ is a **non-locking consistent read** served entirely from the undo-reconstructed snapshot and takes no locks at all, so it never blocks and is never blocked. Locks only come into play for *locking* reads and for writes. This separation — snapshot reads via undo, write/lock conflicts via the lock manager — is the same split I had built by hand in my Lab 6 transaction manager, and recognizing it in InnoDB is what made the engine feel familiar rather than alien.

## 4. Trade-Offs

The table below summarizes how InnoDB's choices differ from Postgres's, dimension by dimension. Working through this comparison is what tied the whole engine together for me.

| Dimension | InnoDB | PostgreSQL |
|---|---|---|
| Row storage | Clustered B+tree — rows live in PK-ordered leaf pages; the table *is* the index | Unordered heap file; all indexes are separate structures |
| PK lookup speed | Very fast — one B+tree descent lands directly on the row data | Index descent then a heap fetch via `ctid` (one extra hop) |
| Secondary index lookup cost | Two tree descents (bookmark lookup): secondary tree → PK → clustered tree, unless covering | One index descent + one heap fetch via `ctid`; index-only scan needs visibility map |
| Update style | In-place update of the row + write a before-image to the **undo log** | Append-only — writes a whole new tuple version, marks the old one dead (no in-place) |
| Space reclamation | Background **purge thread** frees old undo and delete-marked index entries, incrementally | **VACUUM** reclaims dead tuples from the heap (autovacuum, periodic) |
| Old-version storage for MVCC | Dedicated **undo log** / rollback segments, reconstructed on demand | Old versions stored **inline** in the heap alongside live tuples |
| Phantom prevention | **Next-key (record + gap) locks** at REPEATABLE READ block inserts into a range | Serializable Snapshot Isolation (SSI) at SERIALIZABLE; predicate locks via SIReadLocks |
| Buffer eviction | Split-LRU with old/young sublists + `innodb_old_blocks_time` delay | Clock sweep (second-chance) + ring buffers for large scans |

The cleanest way I can phrase the central trade-off: InnoDB optimizes for **read locality and in-place updates**, paying for it with the bookmark-lookup tax on secondary indexes and the bookkeeping of two logs. Postgres optimizes for **cheap, uniform indexes and lock-free updates**, paying for it with the heap-fetch hop and the ongoing cost of VACUUM.

Some practical consequences I drew from the table:

- On a **primary-key-heavy point/range workload**, InnoDB tends to win, because the row data is reached in a single descent with no second hop.
- On a workload dominated by **non-covering secondary-index lookups**, the bookmark-lookup tax is real, and the fix in InnoDB is almost always to widen the secondary index into a **covering** index rather than to fight the engine.
- For **update-heavy** tables, InnoDB's in-place update plus undo avoids the table bloat that Postgres's append-only model produces, but it shifts the cost onto the purge thread and onto contention in the rollback segments; a long-running read view that pins old undo is InnoDB's version of the "long transaction blocks VACUUM" problem.
- For **strict phantom-free range semantics without going to SERIALIZABLE**, InnoDB's REPEATABLE-READ-plus-gap-locks is a genuinely different point in the design space from Postgres, whose REPEATABLE READ (snapshot isolation) does *not* by itself take range locks.

## 5. Experiments

### 5.1 EXPLAIN on the join

My standard revenue-by-city-and-category report joins all four tables: `orders` → `order_items` → `products`, with `customers` supplying the city. Below is a representative MySQL 8 classic-format `EXPLAIN` for that query (modelled on InnoDB's documented optimizer behaviour; the column table is what MySQL prints, with `\G`-style spacing flattened into a grid). Exact reproduction steps are at the end of this section.

```text
mysql> EXPLAIN
    -> SELECT c.city, p.category, SUM(oi.quantity * oi.price) AS revenue
    -> FROM orders o
    -> JOIN customers  c  ON c.id = o.customer_id
    -> JOIN order_items oi ON oi.order_id = o.id
    -> JOIN products    p  ON p.id = oi.product_id
    -> WHERE o.status = 'completed'
    -> GROUP BY c.city, p.category
    -> ORDER BY revenue DESC;

+----+-------------+-------+--------+--------------------+-----------------+---------+-------------------+--------+----------+----------------------------------------------+
| id | select_type | table | type   | possible_keys      | key             | key_len | ref               |   rows | filtered | Extra                                        |
+----+-------------+-------+--------+--------------------+-----------------+---------+-------------------+--------+----------+----------------------------------------------+
|  1 | SIMPLE      | o     | ALL    | PRIMARY,idx_status | NULL            | NULL    | NULL              |  50000 |    20.00 | Using where; Using temporary; Using filesort |
|  1 | SIMPLE      | c     | eq_ref | PRIMARY            | PRIMARY         | 4       | shop.o.customer_id|      1 |   100.00 | NULL                                         |
|  1 | SIMPLE      | oi    | ref    | idx_items_order    | idx_items_order | 4       | shop.o.id         | 750000 |   100.00 | NULL                                         |
|  1 | SIMPLE      | p     | eq_ref | PRIMARY            | PRIMARY         | 4       | shop.oi.product_id|      1 |   100.00 | Using join buffer (hash join)                |
+----+-------------+-------+--------+--------------------+-----------------+---------+-------------------+--------+----------+----------------------------------------------+
4 rows in set, 1 warning (0.00 sec)
```

Reading this row by row:

- **`o` (orders): `type = ALL`** — a full clustered-index scan of all 50,000 orders. The optimizer judged that the `status = 'completed'` predicate is not selective enough (`filtered = 20%`) to make the `idx_status` secondary index worthwhile, so it scans the table. The `Extra` column carries the heavy work: `Using where` (the status filter), `Using temporary` (a temp table for the `GROUP BY`), and `Using filesort` (sorting the grouped result by `revenue DESC`).
- **`c` (customers): `type = eq_ref`** — for each surviving order, exactly one customer is fetched by `PRIMARY` key (`c.id = o.customer_id`). `eq_ref` is the best possible join type for a unique key, and `rows = 1`.
- **`oi` (order_items): `type = ref` using `idx_items_order`** — each order is joined to its line items through the secondary index on `order_id`. The `rows ≈ 750000` estimate reflects the total fan-out across the 750k-row child table; this is the dominant cost of the query.
- **`p` (products): `type = eq_ref` on `PRIMARY`** — each line item resolves to exactly one product by primary key.

A note on `EXPLAIN ANALYZE`: it actually executes the statement and prints the iterator tree with measured timings (`actual time=... rows=... loops=...`) per node. A representative (trimmed) tree for this query looks like this:

```text
-> Sort: revenue DESC  (actual time=812.4..813.0 rows=240 loops=1)
    -> Table scan on <temporary>  (actual time=809.1..809.3 rows=240 loops=1)
        -> Aggregate using temporary table  (actual time=809.0..809.0 rows=240 loops=1)
            -> Nested loop inner join  (actual time=0.21..523.7 rows=748991 loops=1)
                -> Nested loop inner join  (actual time=0.18..210.4 rows=748991 loops=1)
                    -> Nested loop inner join  (actual time=0.10..78.9 rows=10000 loops=1)
                        -> Filter: (o.status = 'completed')  (rows=10000 loops=1)
                            -> Table scan on o  (actual time=0.05..22.1 rows=50000 loops=1)
                        -> Single-row index lookup on c using PRIMARY (id=o.customer_id) (loops=10000)
                    -> Index lookup on oi using idx_items_order (order_id=o.id) (loops=10000)
                -> Single-row index lookup on p using PRIMARY (id=oi.product_id) (loops=748991)
```

This shows the `order_items` index lookup is where wall-clock time concentrates — note `loops=10000` for the lookup that fans out to ~749k rows — exactly as the classic-format row estimate predicted. It also showed that the `Aggregate using temporary table` plus the final `Sort` (the `Using temporary; Using filesort` from the classic plan) were a non-trivial tail at the top of the tree.

![MySQL EXPLAIN of the 4-table join](../../screenshots/innodb-explain.png)
*The MySQL 8 EXPLAIN output for the revenue-by-city-category join: full scan on `orders`, `ref` on `order_items` via `idx_items_order`, `eq_ref` PK lookups for `customers` and `products`.*

### 5.2 Gap-lock blocking demo (two sessions)

This is the headline experiment, because it is the one that let me *see* a gap lock do its job. I opened two MySQL client sessions against the same database, both at the default REPEATABLE READ isolation level, and reproduced a phantom-prevention block on a range of `orders`.

**Session A** takes a locking range read. Crucially, the range `id BETWEEN 20100 AND 20200` causes InnoDB to take **next-key / gap locks** over that entire id interval, including the gaps where no row currently exists:

```text
-- Session A
mysql> SET innodb_lock_wait_timeout = 8;
Query OK, 0 rows affected (0.00 sec)

mysql> BEGIN;
Query OK, 0 rows affected (0.00 sec)

mysql> SELECT * FROM orders WHERE id BETWEEN 20100 AND 20200 FOR UPDATE;
+-------+-------------+-----------+---------------------+--------+
| id    | customer_id | status    | created             | total  |
+-------+-------------+-----------+---------------------+--------+
| 20100 |        1873 | completed | 2025-05-02 10:14:00 | 142.50 |
|  ...  |         ... |    ...    |         ...         |   ...  |
| 20200 |         904 | pending   | 2025-05-02 18:42:00 |  61.00 |
+-------+-------------+-----------+---------------------+--------+
-- transaction A is now OPEN and holds next-key locks over [20100, 20200]
```

**Session B** then tries to INSERT a new order whose id, 20150, falls *inside* the locked gap. There is no row with id 20150 yet — it would be a phantom — and that is exactly what InnoDB refuses to allow while Session A's range lock is held. The INSERT blocks, waiting for the gap lock, until `innodb_lock_wait_timeout` (8 seconds) elapses and it fails:

```text
-- Session B
mysql> BEGIN;
Query OK, 0 rows affected (0.00 sec)

mysql> INSERT INTO orders (id, customer_id, status, created, total)
    -> VALUES (20150, 42, 'pending', '2025-06-23', 99.00);
-- ... hangs here, blocked inside the gap, for ~8 seconds ...
ERROR 1205 (HY000): Lock wait timeout exceeded; try restarting transaction
```

With Session B blocked, running `SHOW ENGINE INNODB STATUS\G` from a third session surfaces the `TRANSACTIONS` section, which names both transactions and shows Session B waiting to acquire a lock on the `orders` primary index inside the gap held by Session A:

```text
-- third session, while B is blocked
mysql> SHOW ENGINE INNODB STATUS\G

------------
TRANSACTIONS
------------
---TRANSACTION 4271, ACTIVE 4 sec inserting
mysql tables in use 1, locked 1
LOCK WAIT 2 lock struct(s), heap size 1136, 1 row lock(s)
MySQL thread id 18, query id 990 localhost insertuser update
INSERT INTO orders (id, customer_id, status, created, total)
  VALUES (20150, 42, 'pending', '2025-06-23', 99.00)
------- TRX HAS BEEN WAITING 4 SEC FOR THIS LOCK TO BE GRANTED:
RECORD LOCKS space id 58 page no 6 n bits 96 index PRIMARY of table `shop`.`orders`
trx id 4271 lock_mode X locks gap before rec insert intention waiting

---TRANSACTION 4269, ACTIVE 31 sec
2 lock struct(s), heap size 1136, 3 row lock(s)
MySQL thread id 17, query id 870 localhost selectuser
-- (this is Session A, holding the next-key locks over the range)
```

The key phrase is `lock_mode X locks gap before rec insert intention waiting`: Session B is trying to get an **insert-intention lock** into a gap that Session A holds, and so it waits.

![Session A acquires the gap lock](../../screenshots/innodb-gap-session-a-1.png)
*Session A: `SELECT ... FOR UPDATE` over the id range 20100–20200 returns and the transaction stays open, holding next-key/gap locks.*

![Session B INSERT blocks inside the gap](../../screenshots/innodb-gap-session-a-2.png)
*Session B: the INSERT of id 20150 hangs inside the locked gap and then fails with `ERROR 1205` after the 8-second timeout.*

![SHOW ENGINE INNODB STATUS gap-lock wait + ERROR 1205](../../screenshots/innodb-gap-lock.png)
*`SHOW ENGINE INNODB STATUS` TRANSACTIONS section showing the insert-intention lock waiting on the gap, alongside the resulting ERROR 1205.*

**Why it blocks (and when it would not).** At REPEATABLE READ, `SELECT ... FOR UPDATE` over a range does not merely lock the existing rows — it takes **next-key locks** that cover the gaps in the range as well. An INSERT must first acquire an *insert-intention* lock on the gap it is about to write into, and an insert-intention lock is incompatible with an existing gap lock held by another transaction. So Session B's INSERT of id 20150 cannot proceed while Session A holds the range, and it waits until the timeout. This is the mechanism by which REPEATABLE READ stops phantom rows from appearing inside a range a transaction has already read.

The contrast at **READ COMMITTED** is the control experiment that proves the gap lock is the cause. With both sessions set to READ COMMITTED, InnoDB does not take gap locks, so the gap over 20150 is unprotected and Session B's INSERT **succeeds immediately** — at the price of allowing a phantom:

```text
-- Session A (READ COMMITTED)
mysql> SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED;
mysql> BEGIN;
mysql> SELECT * FROM orders WHERE id BETWEEN 20100 AND 20200 FOR UPDATE;
-- locks only the rows that actually exist; NO gap locks are taken

-- Session B (READ COMMITTED)
mysql> BEGIN;
mysql> INSERT INTO orders (id, customer_id, status, created, total)
    -> VALUES (20150, 42, 'pending', '2025-06-23', 99.00);
Query OK, 1 row affected (0.00 sec)   -- succeeds instantly: a phantom is allowed
```

The only variable I changed between the blocking run and this run was the isolation level, so the difference in outcome isolates the gap lock as the responsible mechanism.

Exact reproduction steps:

```text
1. Load the schema (customers 10k, products 1k, orders 50k, order_items 750k)
   into a MySQL 8 instance with the default engine (InnoDB) and default
   isolation level (REPEATABLE READ). Confirm with:
       SELECT @@transaction_isolation;   -- expect REPEATABLE-READ
       SELECT @@default_storage_engine;  -- expect InnoDB

2. Open Session A:
       SET innodb_lock_wait_timeout = 8;
       BEGIN;
       SELECT * FROM orders WHERE id BETWEEN 20100 AND 20200 FOR UPDATE;
   (leave this transaction OPEN — do not COMMIT yet)

3. Open Session B and run, within 8 seconds:
       BEGIN;
       INSERT INTO orders (id, customer_id, status, created, total)
       VALUES (20150, 42, 'pending', '2025-06-23', 99.00);
   Observe it BLOCK, then fail with:
       ERROR 1205 (HY000): Lock wait timeout exceeded; try restarting transaction

4. (Optional) From a third session while B is blocked, run
       SHOW ENGINE INNODB STATUS\G
   and read the TRANSACTIONS section for the "insert intention waiting" entry.

5. COMMIT or ROLLBACK Session A to release the locks. Session B can then retry.

6. To show the contrast, repeat steps 2–3 but first run in BOTH sessions:
       SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED;
   Now Session B's INSERT succeeds immediately (no gap lock => phantom allowed).

7. (Optional) Confirm the level in effect at any point with:
       SELECT @@transaction_isolation;
   so the two runs are unambiguously distinguished in the captured output.
```

The fact that exactly one changed setting flips the behavior from "blocks with ERROR 1205" to "succeeds instantly" is what makes this a clean, controlled demonstration rather than an anecdote.

## 6. Key Learnings

- **The clustered index IS the table.** InnoDB has no separate heap; rows live in primary-key order in the leaf pages of a B+tree, which makes PK lookups and PK range scans cheap because the row data is right there in the leaf.
- **A secondary index lookup is a double lookup.** Secondary index leaves store the primary key, not a physical pointer, so a non-covering lookup pays for two B+tree descents (the bookmark lookup). Covering indexes are the cure, and `EXPLAIN` shows `Using index` when they apply.
- **Two logs serve two genuinely different needs.** Redo rolls *forward* committed work after a crash for durability; undo rolls *back* uncommitted work and reconstructs old versions for MVCC reads. Neither one can do the other's job, which is why both exist.
- **Gap locks are how REPEATABLE READ kills phantoms.** Next-key locks (record + preceding gap) on a range mean a concurrent INSERT into that range's gap blocks — I watched it happen and fail with ERROR 1205. READ COMMITTED drops gap locks and lets the phantom through.
- **In-place update + undo vs append-only heap is the deepest InnoDB/Postgres split.** InnoDB updates rows in place and keeps the old image in undo (reclaimed by purge); Postgres writes a whole new tuple version and relies on VACUUM. The same MVCC goal, two opposite implementations.
- **The buffer pool's old/young split LRU is deliberate scan resistance** — single-touch scan pages enter the old sublist and age out without evicting the hot working set, only promoting on a delayed second access.
- **MVCC reads and lock-based writes are two separate systems.** A plain `SELECT` is served from an undo-reconstructed snapshot and takes no locks; only `FOR UPDATE`/`FOR SHARE` and writes engage the lock manager. Confusing the two is the most common reason people misread when InnoDB does or does not block.
- **The primary key is a physical-layout decision, not just a logical one.** A short, monotonic key keeps the clustered tree dense and keeps every secondary index small, because all of them embed the primary key in their leaves.

## Connections to my course labs

The two labs below are the ones whose code maps most directly onto what InnoDB does in production, and studying InnoDB made me realize how close my toy implementations were to the real engine.

| Lab | My files | What it maps to in InnoDB |
|---|---|---|
| Lab 4 — B-Tree from scratch | [../../lab_sessions/lab_4.txt](../../lab_sessions/lab_4.txt) + [../../index/main.cpp](../../index/main.cpp) | InnoDB's clustered index and every secondary index are B+trees with split/merge on insert/delete — exactly the structure I built, with rows stored at the leaves keyed in sorted order. |
| Lab 6 — MVCC + Two-Phase Locking + deadlock detection | [../../lab_sessions/lab_6.txt](../../lab_sessions/lab_6.txt) | The record/gap locks, the read view for snapshot reads, and lock-wait / deadlock handling are the production version of the lock manager and version-visibility rules I implemented. |

The toy lock manager I wrote in Lab 6 used strict two-phase locking with a waits-for graph for deadlock detection, and its row-version visibility rule (compare a version's creator/invalidator against the transaction's snapshot id) is conceptually identical to how InnoDB's read view decides which undo-reconstructed version a consistent read should see. The gap-lock demo in Section 5.2 was the moment my lab work became concrete: the "insert intention waiting" entry in `SHOW ENGINE INNODB STATUS` is precisely a real-world instance of the lock-wait state my lab's manager modeled, only here it is protecting against phantoms across a key range rather than guarding a single row.

The biggest gap between my toy and the real engine was *granularity and structure*: my lab locked logical keys, while InnoDB locks index records and the gaps between them, and my lab kept versions in a simple list, while InnoDB threads them through undo records reachable by a per-row roll-pointer. But the control flow — take a snapshot at statement/transaction start, read through the snapshot without blocking, take locks only for writes and locking reads, detect a wait that cannot be granted, and time it out or break a deadlock cycle — is the same skeleton in both. Re-reading my Lab 4 B-Tree code (`index/main.cpp`) with InnoDB in mind, I also now understand why my node split logic and the `minDegree`/`maxKeys` bookkeeping matter so much at scale: that is the exact machinery deciding when a 16 KB InnoDB page splits, and getting it wrong is what produces the half-empty pages a random primary key causes.
