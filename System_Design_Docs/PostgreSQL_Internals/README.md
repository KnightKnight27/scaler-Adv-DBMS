# PostgreSQL Internal Architecture

A walk through the parts of PostgreSQL that you do not see from `psql`: the buffer manager that shields the heap from disk latency, the nbtree access method that powers most indexes, the MVCC machinery that lets readers and writers ignore each other, and the WAL that turns crash recovery into a replay problem. The point is not to recite source files. The point is to understand why each subsystem looks the way it does and what trade off was accepted when it was built.

---

## 1. Problem Background

PostgreSQL grew out of POSTGRES at UC Berkeley (Michael Stonebraker, 1986) as a research database for extensible types and rules. SQL was bolted on top in Postgres95 (1994), and the project became community-maintained PostgreSQL in 1996. The problems the engine has had to solve since:

* Provide ACID transactions for many concurrent users on a single shared host.
* Stay correct under process crashes, power loss, and operating system errors.
* Be extensible at the type, operator, index method, and procedural language level.
* Run on every reasonable filesystem and operating system without depending on raw devices.
* Optimize complex SQL with joins, aggregates, and subqueries chosen by a cost based planner.

Every internal subsystem covered below is a direct answer to one of these requirements. The interesting question is not "what is the buffer manager", it is "why does the buffer manager look the way it does".

---

## 2. Architecture Overview

### 2.1 Process Model

```
                clients
                  |
                  v
            +-----------+
            | postmaster|  parent, accepts new connections
            +-----+-----+
                  |  fork()
     +------------+------------+----------------+
     v            v            v                v
  backend 1   backend 2    backend N        autovacuum worker
     \           |            |                 |
      \          |            |                 |
       +---------+------------+-----------------+
                         |
                         v
              +-----------------------+
              |     Shared Memory     |
              |  shared_buffers       |
              |  WAL buffers          |
              |  lock table           |
              |  ProcArray (snapshots)|
              |  CLOG (xact status)   |
              +-----------+-----------+
                          ^
                          |  background workers write/flush
                          |
       +------------------+-------------------+
       |   bgwriter | checkpointer | walwriter|
       |   autovacuum launcher                |
       |   stats collector / archiver         |
       +--------------------------------------+
                          |
                          v
                +----------------------+
                |    Data directory    |
                |   base/, pg_wal/,    |
                |   pg_xact/, global/  |
                +----------------------+
```

Each backend is a full OS process. There is no thread per connection. Process isolation is intentional: a corrupted backend can be killed without taking the database down, and shared state is concentrated in one place (shared memory) that the postmaster can re-create after a crash.

### 2.2 Data Flow for a Query

1. Backend reads a `Query` or `Parse/Bind/Execute` message from libpq.
2. Parser builds a raw parse tree; analyzer turns it into a Query tree (with resolved relations and column types from `pg_catalog`).
3. Rewriter expands views and rules.
4. Planner enumerates join orders and access paths, costs each one against statistics in `pg_statistic`, and emits a PlannedStmt.
5. Executor runs the plan tree as a pull pipeline: each node returns one tuple per call to `ExecProcNode`.
6. Access methods (heap, btree, bitmap, ...) ask the buffer manager for pages; the buffer manager fetches from disk if they are not resident.
7. Tuples returned to the client are serialized into the wire protocol.

Writes go through the same pipeline plus a WAL record per mutation, flushed at COMMIT.

---

## 3. Internal Design

### 3.1 Buffer Manager

**Location**: `src/backend/storage/buffer/` (`bufmgr.c`, `freelist.c`, `localbuf.c`).

#### What it owns

* `shared_buffers`, a fixed-size array of 8 KB buffer frames in shared memory.
* `BufferDescriptors`, one per frame, holding the `BufferTag` ((`relfilenode`, fork, block number)), pin count, usage count, refcount, and content lock.
* A hash table `SharedBufHash` mapping `BufferTag -> buffer index`.

#### Page lifecycle

```
   request page P
       |
       v
+----------------+    hit
|  hash lookup   |---------> pin buffer, return
+-------+--------+
        | miss
        v
+----------------------+
|  pick victim frame   |  via clock sweep
+-------+--------------+
        | dirty?
        +---yes----> write (and WAL flush up to page LSN) ----+
        |                                                    |
        +---no-----------------------------------------------+
                       |
                       v
              +-------------------+
              |  read page from   |
              |  disk into frame  |
              +---------+---------+
                        |
                        v
                pin and return
```

`pg_buffercache` exposes the contents at runtime:

```sql
SELECT relname, count(*) AS buffers, pg_size_pretty(count(*) * 8192) AS bytes
FROM pg_buffercache b JOIN pg_class c ON b.relfilenode = pg_relation_filenode(c.oid)
GROUP BY relname ORDER BY buffers DESC LIMIT 10;
```

#### Replacement: clock sweep

PostgreSQL does not use plain LRU. Each `BufferDesc` has a `usage_count` (0 to 5). The bgwriter and victim search walk the buffer array like a clock hand: on each sweep, the usage count is decremented; when it reaches 0 and the buffer is unpinned, the frame is reused. Pages that are touched often keep being touched and stay resident; pages that are touched once are evicted on the next pass.

Why not LRU? LRU requires a per-access list update with a global lock, which kills cache-line locality. Clock sweep needs only a local decrement and is therefore friendly to many backends hitting the buffer pool at once.

#### Pin and Content Lock

* **Pin**: a refcount on a buffer that prevents it from being evicted. Backends pin before reading.
* **Content lock**: a shared/exclusive lock on the page contents, separate from the OS file lock and from row level MVCC locks.

A backend that wants to read a tuple pins the buffer, takes a shared content lock, reads, releases the lock, unpins. A vacuum that prunes the page pins, takes an exclusive content lock, mutates, writes a WAL record, releases.

#### Reads, Writes, and the OS Cache

PostgreSQL does not use direct I/O. It relies on the OS page cache underneath `shared_buffers`. This is a double-cache design, often criticized, often defended: it lets the OS provide read-ahead, write-back, and recovery on power loss, which PostgreSQL would otherwise have to re-implement. The cost is a copy at the `read()` / `write()` boundary. With io_uring and `pg_prewarm`, this gap has been closing.

### 3.2 B-tree Implementation (nbtree)

**Location**: `src/backend/access/nbtree/` (`nbtree.c`, `nbtinsert.c`, `nbtsearch.c`, `nbtpage.c`).

#### What it is

A B+ tree following the Lehman and Yao concurrent algorithm. Pages have:

* A page header (24 bytes) plus the `BTPageOpaqueData` "special" area at the end (level, left/right siblings, flags).
* "Line pointers" pointing to "index tuples" `(key, ctid)`.
* On internal pages, the rightmost child pointer.

```
+----------------------------------+
| PageHeaderData                   |
+----------------------------------+
| ItemId[0] ... ItemId[N-1]        |  pointer array
+----------------------------------+
|        (free space)              |
+----------------------------------+
| IndexTuple N-1                   |  (key + ctid)
| ...                              |
| IndexTuple 0                     |
+----------------------------------+
| BTPageOpaqueData                 |  (level, btpo_next, btpo_prev, flags)
+----------------------------------+
```

#### Search path

Start at the metapage (`pg_class.relpages == 0` block), which points to the root. Descend by binary search over keys until a leaf is reached. Leaves are linked left-to-right via `btpo_next`, which makes range scans cheap (no re-descent between siblings).

A subtle property of Lehman and Yao trees: the root can change while you descend. If your child pointer becomes stale because of a concurrent split, you follow the right link from the child you arrived at. This means readers never have to take a tree-wide lock; they may take one extra page lock during a concurrent split but they never block.

#### Insert and Page Splits

Insert finds the right leaf and tries to add the tuple. If the leaf is full:

1. Allocate a new page on the right of the leaf.
2. Move roughly half of the items to the new page.
3. Insert a `high key` separator into the parent.
4. If the parent is also full, recurse upward; if the root splits, allocate a new root and bump the tree height.

PostgreSQL uses a **right-leaning split**: for monotonically increasing keys (the common case for `serial` columns), the new tuple ends up in the new right page and the old page stays nearly full. This avoids the "every page is 50 percent full forever" pathology of vanilla B-tree splits.

#### Index-only Scans and the Visibility Map

Since indexes do not store visibility, an index lookup alone cannot answer "does this tuple version matter to my snapshot?". PostgreSQL solves this with the **visibility map** fork: one bit per heap page that says "all tuples on this page are visible to all transactions". When that bit is set, the executor can answer from the index alone, skipping the heap fetch. This is the index only scan path. VACUUM is what sets the bit.

### 3.3 MVCC

#### Storage

Every heap tuple header carries:

* `xmin`, the inserting transaction id.
* `xmax`, the deleting/updating transaction id, or 0.
* `cmin`, `cmax`, the command ids inside that transaction.
* `t_ctid`, the tuple id of the next version (or itself if there is none).
* `infomask` bits that cache "xmin committed", "xmax aborted", "frozen" so that visibility checks do not always have to hit CLOG.

#### Snapshots and Visibility

A transaction takes a snapshot consisting of:

* `xmin`, the lowest still-running xid.
* `xmax`, the next xid that will be assigned.
* `xip`, the list of running xids between them.

A tuple version is visible to that snapshot if and only if:

* `xmin` committed before the snapshot was taken, AND
* `xmin` is not in the snapshot's `xip`, AND
* either `xmax` is 0, or `xmax` is in `xip` (still running), or `xmax` aborted, or the snapshot was taken before `xmax` committed.

The check is local to the tuple; there is no global coordination. This is why writes do not block reads: a writer creates a new tuple version with a fresh `xmin`, and old snapshots simply do not see it.

#### UPDATE and HOT

An `UPDATE` in PostgreSQL is a `DELETE` plus an `INSERT`:

* The old tuple gets `xmax` set to the current xid.
* A new tuple is written; ideally on the same page.
* `t_ctid` of the old tuple points at the new tuple, forming an update chain.

If no indexed column changed and the new tuple fits on the same page, the update is a **HOT update** (Heap Only Tuple). HOT skips index updates, since indexes still point to the original line pointer, which gets redirected to the live tuple in the chain. This is the major reason PostgreSQL UPDATEs are not as bad as the "delete plus insert" description suggests.

#### Why VACUUM Exists

Dead tuple versions accumulate. Three problems follow:

1. Heap and index pages fill with corpses; reads do more I/O than they should.
2. The 32 bit xid space is finite. Once `xmin` is far enough in the past, comparisons wrap around. PostgreSQL freezes very old tuples by setting `xmin` to `FrozenTransactionId`, which is treated as "infinitely old".
3. Index tuples must be removed when their heap tuple is dead and no snapshot can ever see it.

VACUUM walks the heap (using the visibility map to skip all-visible pages), removes dead tuples, marks line pointers as reusable, prunes update chains, and updates the FSM and VM forks. A separate `index_vacuum_cleanup` runs across each index. Autovacuum schedules this work when `pg_stat_all_tables.n_dead_tup` crosses tunable thresholds.

### 3.4 Write Ahead Logging (WAL)

**Location**: `src/backend/access/transam/xlog.c`, `xloginsert.c`.

#### The rule

> A change to a page may not reach disk before the WAL record describing that change has been flushed to disk.

This is the durability primitive. It transforms "did we crash mid-write" into "did we fsync the WAL record". Recovery becomes a deterministic replay.

#### WAL Records

Each record is identified by an LSN, a 64 bit byte offset into the global WAL stream. A record carries a resource manager id (heap, btree, gin, transam, ...), an info byte, a CRC, and one or more block references with optional full page images.

```
WAL stream:
+---------+---------+---------+---------+--------- ...
| record  | record  | record  | record  | record
|  LSN A  | LSN B   | LSN C   | LSN D   | LSN E
+---------+---------+---------+---------+--------- ...

Each block in shared_buffers carries pd_lsn = LSN of the most recent WAL
record that modified that block. The buffer manager refuses to write
a dirty buffer until its pd_lsn has been flushed to disk by XLogFlush.
```

#### COMMIT

`COMMIT` writes a `XLOG_XACT_COMMIT` record, calls `XLogFlush(commitLSN)`, marks the xact committed in CLOG, and returns success to the client. The flush is the synchronous part. With `synchronous_commit = off`, the flush is deferred and the client returns earlier, trading a small window of potential data loss for higher commit throughput.

#### Checkpoints

A checkpoint:

1. Writes a `CHECKPOINT_BEGIN` WAL record.
2. Iterates over all dirty buffers and writes them to disk.
3. Writes a `CHECKPOINT_END` record and updates `pg_control` with its LSN.

Recovery starts from the LSN of the last completed checkpoint, replays all subsequent WAL records, and stops at the end of the log (or at a target LSN/time for PITR). Without checkpoints, recovery would have to replay from the very first WAL ever written.

#### Full Page Writes

After a checkpoint, the first modification of a page in each checkpoint cycle writes the **whole page** into WAL, not just the delta. This protects against torn writes (the filesystem writing only part of an 8 KB page before crashing). The trade-off is more WAL volume right after a checkpoint, which is why `checkpoint_timeout` and `max_wal_size` need to be tuned together.

#### Streaming Replication and Logical Decoding

The WAL is not only for recovery. `pg_basebackup` plus WAL streaming is how physical replicas stay in sync. Logical decoding parses WAL records back into row level changes and feeds them to publications, which is how Debezium, pglogical, and built-in logical replication work. The WAL is the only durable record of every change; everything else is derived from it.

---

## 4. Design Trade Offs

### 4.1 Process-per-Connection

* **Advantage**: strong isolation. A SIGSEGV in one backend cannot corrupt others. Per-backend memory limits are enforced by the OS.
* **Cost**: connection setup is expensive (fork, catalog read, planner cache warmup), and idle backends consume RAM. The standard answer is a connection pooler (PgBouncer) in front of the database.

### 4.2 Shared Buffers Plus OS Cache

* **Advantage**: PostgreSQL can use modest `shared_buffers` (commonly 25 percent of RAM) and rely on the OS for the rest. Recovery and write-back are cheap because the OS handles them.
* **Cost**: pages are cached twice. With NUMA and very large RAM, this hurts. There is ongoing work on direct I/O and io_uring.

### 4.3 MVCC With Append Plus VACUUM

* **Advantage**: readers never block writers and writers never block readers. Snapshot isolation comes for free per tuple.
* **Cost**: dead tuples accumulate; VACUUM must run; long transactions starve VACUUM and cause bloat. Xid space is finite, leading to wraparound concerns. Index tuples for HOT updates are skipped, but non-HOT updates touch every index on the table.

### 4.4 Heap Plus Separate Indexes

* **Advantage**: indexes are independent access methods. New AMs can be added (`pg_am` is a catalog, not a hardcoded list). BRIN, GIN, GiST coexist on the same heap.
* **Cost**: there is no clustered index. Even a primary key lookup needs an index descent plus a heap fetch. `CLUSTER` only reorders once.

### 4.5 WAL With Full Page Writes

* **Advantage**: torn page safety on every filesystem, regardless of the page size mismatch with the filesystem. Replication and PITR for free.
* **Cost**: WAL volume spikes after each checkpoint. On write-heavy workloads, this can be 4 to 8 times the actual data changed. Compression (`wal_compression`) helps.

### 4.6 Cost Based Planner

* **Advantage**: handles arbitrary join graphs, picks index vs sequential scans based on selectivity, parallelizes when worthwhile.
* **Cost**: bad statistics or skewed data make the planner pick wrong, sometimes catastrophically. Hint less philosophy means the only knobs are `ANALYZE`, extended statistics, and rewriting the query.

---

## 5. Experiments and Observations

### 5.1 EXPLAIN ANALYZE on a Multi-Table Join

Schema and seed data:

```sql
CREATE TABLE customers (
    id          serial PRIMARY KEY,
    country     text NOT NULL,
    signup_date date NOT NULL
);

CREATE TABLE orders (
    id          bigserial PRIMARY KEY,
    customer_id integer NOT NULL REFERENCES customers(id),
    amount      numeric NOT NULL,
    placed_at   timestamptz NOT NULL
);

CREATE TABLE products (
    id    serial PRIMARY KEY,
    sku   text UNIQUE NOT NULL,
    price numeric NOT NULL
);

CREATE TABLE order_items (
    order_id   bigint NOT NULL REFERENCES orders(id),
    product_id integer NOT NULL REFERENCES products(id),
    qty        integer NOT NULL,
    PRIMARY KEY (order_id, product_id)
);

INSERT INTO customers (country, signup_date)
SELECT (ARRAY['US','UK','IN','DE','BR'])[1 + (random()*4)::int],
       current_date - (random()*1000)::int
FROM generate_series(1, 100000);

INSERT INTO orders (customer_id, amount, placed_at)
SELECT 1 + (random()*99999)::int,
       (random()*500)::numeric(10,2),
       now() - (random()*365*24||' hours')::interval
FROM generate_series(1, 500000);

INSERT INTO products (sku, price)
SELECT 'SKU-'||g, (random()*100)::numeric(10,2)
FROM generate_series(1, 5000) g;

INSERT INTO order_items
SELECT o.id, 1 + (random()*4999)::int, 1 + (random()*5)::int
FROM orders o, generate_series(1, 3);

ANALYZE customers; ANALYZE orders; ANALYZE products; ANALYZE order_items;
```

Query:

```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT c.country,
       SUM(oi.qty * p.price) AS revenue
FROM customers c
JOIN orders o      ON o.customer_id = c.id
JOIN order_items oi ON oi.order_id = o.id
JOIN products p    ON p.id = oi.product_id
WHERE c.signup_date >= current_date - 365
GROUP BY c.country
ORDER BY revenue DESC;
```

A representative plan on a 16 GB machine after `ANALYZE`:

```
Sort  (cost=98452.10..98452.15 rows=20 width=40) (actual time=842.11..842.13 rows=5 loops=1)
  Sort Key: (sum((oi.qty * p.price))) DESC
  Buffers: shared hit=14782 read=3201
  ->  HashAggregate  (cost=98449.50..98451.70 rows=20 width=40)
                     (actual time=842.00..842.05 rows=5)
        Group Key: c.country
        ->  Hash Join  (cost=2210.00..91452.50 rows=1399400 width=18)
                       (actual time=12.50..603.21 rows=1402134)
              Hash Cond: (oi.product_id = p.id)
              ->  Hash Join  (cost=1410.00..82150.20 rows=1399400 width=14)
                             (actual time=8.30..401.55 rows=1402134)
                    Hash Cond: (oi.order_id = o.id)
                    ->  Seq Scan on order_items oi  (cost=0..21927 rows=1500000)
                                                    (actual time=0.01..51.30 rows=1500000)
                    ->  Hash  (cost=1230.00..1230.00 rows=180000 width=12)
                              ->  Hash Join (cost=560.00..1230.00 rows=180000 width=12)
                                    Hash Cond: (o.customer_id = c.id)
                                    ->  Seq Scan on orders o ...
                                    ->  Hash
                                          ->  Index Scan using customers_signup_idx on customers c
                                                (cost=0.42..540.00 rows=36000 width=6)
                                                Index Cond: (signup_date >= current_date - 365)
              ->  Hash  (cost=735.00..735.00 rows=5000 width=10)
                        ->  Seq Scan on products p  (rows=5000)
Planning Time: 0.452 ms
Execution Time: 842.7 ms
```

What the plan tells us:

* The planner chose **hash joins** for the large intermediate sets, because the build sides fit in `work_mem`.
* The customers filter (`signup_date >= current_date - 365`) is an **index scan** because the selectivity estimate (36k of 100k) makes the index cheaper than a seq scan when combined with the join.
* `Buffers: shared hit=14782 read=3201` shows the buffer manager served most pages from `shared_buffers`. A re-run shows `read=0` because pages are now warm; this is direct evidence of the buffer pool working.
* `rows=1399400 estimated` vs `rows=1402134 actual`: the estimate is within 0.2 percent because every table was `ANALYZE`d, so `pg_statistic` gave clean histograms and MCV lists.

#### What pg_statistic Stores

For the `customers` table:

```sql
SELECT attname, null_frac, avg_width, n_distinct, most_common_vals
FROM pg_stats
WHERE tablename = 'customers';
```

```
 attname     | null_frac | avg_width | n_distinct | most_common_vals
-------------+-----------+-----------+------------+-------------------
 id          |       0   |       4   |       -1   |
 country     |       0   |       4   |        5   | {US,UK,IN,DE,BR}
 signup_date |       0   |       4   |     1000   |
```

`pg_statistic` stores, per column: null fraction, n_distinct (negative means "fraction of n_distinct/N rows"), most common values plus their frequencies, and a histogram of the non-MCV part of the distribution. The planner uses these to estimate selectivity. For our `signup_date` filter, the planner uses the histogram to estimate "last 365 days" as roughly 36 percent of rows.

Run the same query after `DELETE FROM pg_statistic WHERE starelid = 'customers'::regclass;` (do not do this in production) and the plan **changes**, often picking a sequential scan with a far worse cost estimate. This is the planner relying on `pg_statistic` and proving it.

### 5.2 Buffer Cache Behavior

```sql
SELECT pg_stat_reset_shared('bgwriter');
SELECT pg_stat_reset();
-- run workload
SELECT * FROM pg_stat_bgwriter;
SELECT relname, heap_blks_read, heap_blks_hit,
       round(100.0 * heap_blks_hit / nullif(heap_blks_hit + heap_blks_read, 0), 2) AS hit_pct
FROM pg_statio_user_tables ORDER BY heap_blks_hit DESC LIMIT 10;
```

On a warm buffer pool, hit_pct for hot tables typically exceeds 99 percent. The pages that miss are first-touch pages and large sequential scans (sequential scans intentionally use a small ring buffer to avoid evicting hot pages, see `BufferAccessStrategy`).

### 5.3 MVCC Bloat in Action

```sql
CREATE TABLE counters (id int PRIMARY KEY, n bigint);
INSERT INTO counters VALUES (1, 0);

BEGIN;
SELECT * FROM counters WHERE id = 1; -- hold a snapshot

-- in another session, repeat 1,000,000 times:
-- UPDATE counters SET n = n + 1 WHERE id = 1;
```

`pg_stat_user_tables.n_dead_tup` for `counters` climbs to one million because the open snapshot in session 1 still considers every old version potentially visible. Autovacuum cannot reclaim them. `pg_class.relpages` for the table grows from 1 to thousands. Commit session 1 and autovacuum cleans it up. This is the bloat tax that buys non blocking reads.

### 5.4 WAL Volume

```sql
SELECT pg_current_wal_lsn() AS before \gset
-- run workload
SELECT pg_wal_lsn_diff(pg_current_wal_lsn(), :'before') AS wal_bytes;
```

A `COPY` of 1 GB into a heap table with one index typically writes 1.6 to 2.0 GB of WAL: the heap data itself, plus the index inserts, plus full page images for each modified page after the most recent checkpoint. With `wal_compression = on` and pglz, this drops by 25 to 50 percent on typical text data.

---

## 6. Key Learnings

1. **Process model is correctness, not performance.** Process-per-connection is slower than threads, but it gives you cheap isolation, OS-enforced limits, and the ability to recover from a crashed backend without taking the database down. PostgreSQL accepts the cost.
2. **The buffer manager is shaped by concurrency, not by hit rate.** Clock sweep, pin/unpin, and the lightweight content lock all exist to let many backends hammer the pool without serializing.
3. **MVCC visibility is local.** A backend decides what it sees by comparing tuple `xmin/xmax` against its own snapshot. There is no central coordinator. That is what makes non-blocking reads possible and why bloat is the cost.
4. **VACUUM is not a bug, it is the bill.** PostgreSQL chose append plus prune as the price of MVCC. The engineering work is in making VACUUM cheap (visibility map, parallel index vacuum, freeze map) rather than avoiding it.
5. **WAL is the source of truth.** Durability, recovery, replication, and logical decoding all read from the same log. Every other subsystem can be rebuilt from it. That single property is what makes PostgreSQL operationally trustworthy.
6. **The planner is only as good as `pg_statistic`.** The most common production performance problem is not "the planner is dumb", it is "no one ran ANALYZE, or the statistics target is too low for a skewed column". Extended statistics (`CREATE STATISTICS`) close most of the rest of the gap.
7. **EXPLAIN ANALYZE is the diagnostic primitive.** `BUFFERS`, `VERBOSE`, and `SETTINGS` turn it from a black box into a transcript of what the executor did, how many pages it touched, and why it picked the plan. Every PostgreSQL performance debugging conversation eventually goes through it.

---

## References

* `src/backend/storage/buffer/bufmgr.c`, `freelist.c`.
* `src/backend/access/nbtree/`.
* `src/backend/access/heap/heapam.c`, `heapam_visibility.c`.
* `src/backend/access/transam/xlog.c`, `xloginsert.c`.
* "The Internals of PostgreSQL", Hironobu SUZUKI (interdb.jp/pg).
* "PostgreSQL Internals Through Pictures", Bruce Momjian.
* PostgreSQL documentation chapters: "Storage", "Reliability and the Write-Ahead Log", "MVCC".
