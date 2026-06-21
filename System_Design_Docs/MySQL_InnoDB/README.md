# MySQL InnoDB — Clustered Indexes, Buffer Pool, Undo/Redo Logs and Locking

**Name:** Pratyush Mohanty
**Roll No.:** 24BCS10238
**Version:** MySQL 8.0.42 (InnoDB)
**Data used:** the same `users` (500,000 rows) and `orders` (1,000,000 rows) as the PostgreSQL doc, so the two engines can be compared on the same workload.

---

## 1. Setup

I rebuilt the same two tables in InnoDB. The only thing worth noting is how the rows were generated: MySQL has no `generate_series`, so I made a 10-row digit table and cross-joined it with itself six times to produce a million numbers, then filtered.

```sql
CREATE TABLE users (
    id INT PRIMARY KEY, name VARCHAR(64), email VARCHAR(128),
    age INT, city VARCHAR(32), created_at DATE
) ENGINE=InnoDB;

CREATE TABLE orders (
    order_id INT PRIMARY KEY AUTO_INCREMENT,
    user_id INT NOT NULL, amount DECIMAL(10,2),
    status VARCHAR(16), order_date DATE,
    CONSTRAINT fk_user FOREIGN KEY (user_id) REFERENCES users(id)
) ENGINE=InnoDB;
```

Relevant defaults on this install:

| Setting | Value |
|---------|-------|
| `innodb_page_size` | 16384 (16 KB) |
| `innodb_buffer_pool_size` | 128 MB (8,192 pages) |
| `innodb_redo_log_capacity` | 100 MB |
| `innodb_flush_log_at_trx_commit` | 1 (flush on every commit) |
| `innodb_doublewrite` | ON |
| `transaction_isolation` | REPEATABLE-READ |

The page size is the first difference from PostgreSQL (16 KB vs 8 KB), and the default isolation level is the second (REPEATABLE READ vs PostgreSQL's READ COMMITTED).

---

## 2. How InnoDB stores a table

The single most important fact about InnoDB: **the table _is_ its primary-key index.** There is no separate heap. The rows live in the leaf pages of a B-tree keyed on the primary key. This is called a clustered index. PostgreSQL does the opposite, it stores rows in an unordered heap and every index (including the PK) points into it.

Looking at the indexes InnoDB actually built:

```sql
SELECT t.name AS tbl, i.name AS index_name, i.n_fields, i.page_no AS root_page
FROM information_schema.innodb_indexes i
JOIN information_schema.innodb_tables t ON i.table_id=t.table_id
WHERE t.name LIKE 'dbms_lab/%';
```

```
 tbl              | index_name      | n_fields | root_page
 dbms_lab/users   | PRIMARY         |    8     |    4         <- clustered: holds the whole row
 dbms_lab/orders  | PRIMARY         |    7     |    4         <- clustered
 dbms_lab/orders  | fk_user         |    2     |    5         <- secondary (auto-made for the FK)
 dbms_lab/seq     | GEN_CLUST_INDEX |    4     |    4         <- see below
```

**Observation:** The `users` PRIMARY index has `n_fields=8`, which is more than the table's 6 columns, because the clustered leaf also stores the two hidden bookkeeping columns every InnoDB row carries: `DB_TRX_ID` (the transaction that last changed the row) and `DB_ROLL_PTR` (a pointer into the undo log). Those two columns are what make MVCC work, and they show up again in sections 7 and 8.

The `seq` helper table is a nice accident. I never gave it a primary key, and InnoDB couldn't store a table without a clustered index, so it invented a hidden one called `GEN_CLUST_INDEX` on an internal 6-byte row id. InnoDB will always cluster on _something_: your PK, else the first non-null unique key, else a hidden row id.

On-disk sizes:

```sql
SELECT table_name, ROUND(data_length/1024/1024,1) AS data_mb,
       ROUND(index_length/1024/1024,1) AS index_mb
FROM information_schema.tables WHERE table_schema='dbms_lab';
```

```
 orders | 46.6 MB data | 19.5 MB index
 users  | 68.6 MB data |  0.0 MB index
```

**Observation:** For `users`, `data_mb` _is_ the clustered index (the whole table), and `index_mb` is 0 because there were no secondary indexes yet. The same 500,000 rows took 41 MB as a PostgreSQL heap and 68.6 MB as an InnoDB clustered index. InnoDB's per-row overhead is higher, partly the hidden columns, partly that the clustered B-tree leaves are not packed as tightly as a heap.

---

## 3. Secondary indexes

A secondary index in InnoDB does **not** store a physical row pointer. Its leaf entries store the indexed column(s) plus the **primary key value**. To fetch any column that isn't in the secondary index, InnoDB has to take the PK it found and do a second lookup in the clustered index. This second hop is the cost of the clustered design.

I added an index on `city` and ran two near-identical queries:

```sql
CREATE INDEX idx_city ON users(city);

EXPLAIN SELECT id, city       FROM users WHERE city='Mumbai';  -- A
EXPLAIN SELECT id, city, name FROM users WHERE city='Mumbai';  -- B
```

```
A:  key=idx_city  ... Extra: Using index      <- answered entirely from the secondary index
B:  key=idx_city  ... Extra: NULL              <- had to go back to the clustered index for `name`
```

**Observation:** Query A is a covering index scan: `id` and `city` are both in `idx_city` (`id` is there because every secondary entry carries the PK), so InnoDB never touches the clustered index. Query B also needs `name`, which isn't in the index, so for every matching row it takes the PK from the secondary entry and looks it up in the clustered index. Same access path, but B does roughly 193,000 extra clustered-index lookups. This is why "add the column to the index to make it covering" is such a common MySQL tuning move.

---

## 4. Buffer pool

InnoDB's equivalent of PostgreSQL's `shared_buffers` is the buffer pool: a fixed region of memory holding 16 KB pages. Both reads and writes go through it.

```sql
SELECT POOL_SIZE AS pages, DATABASE_PAGES AS used, FREE_BUFFERS AS free, HIT_RATE
FROM information_schema.innodb_buffer_pool_stats;
```

```
 pages=8191 | used=6804 | free=641 | HIT_RATE=80/1000
```

```sql
SELECT table_name, COUNT(*) AS pages, ROUND(COUNT(*)*16384/1024/1024,1) AS cached_mb
FROM information_schema.innodb_buffer_page WHERE table_name LIKE '%dbms_lab%'
GROUP BY table_name ORDER BY pages DESC;
```

```
 `dbms_lab`.`users`  | 3892 pages | 60.8 MB
 `dbms_lab`.`orders` | 1426 pages | 22.3 MB
```

**Observation:** Most of `users` is resident after the queries above. The one design difference worth calling out: PostgreSQL uses a clock sweep, while InnoDB uses an **LRU list split into two sublists** (a "young" and an "old" end). A newly read page goes into the _old_ end first; it's only promoted to the young end if it gets read again after a short delay. This is specifically to stop a single large scan (like reading all of `orders` once) from flushing the genuinely hot pages out of the pool. PostgreSQL solves the same problem with its ring-buffer strategy for big scans. Same goal, different mechanism.

---

## 5. Redo log

The redo log is InnoDB's durability mechanism, the same idea as PostgreSQL's WAL: write the change to a sequential log and fsync that on commit, then write the actual data pages later. With `innodb_flush_log_at_trx_commit=1`, every commit flushes the redo log to disk.

I measured redo volume by reading the log sequence number (LSN) from `SHOW ENGINE INNODB STATUS` before and after an update:

```sql
UPDATE orders SET status='shipped' WHERE order_id <= 50000;
```

```
LSN before: 244,977,015
LSN after : 249,562,700
Redo generated: 4,585,685 bytes  (~4.4 MB, about 92 bytes per row)
```

**Observation:** This is the most interesting cross-engine number in the whole doc. The _identical_ update (50,000 rows) generated **15 MB of WAL in PostgreSQL but only 4.4 MB of redo in InnoDB.** The reason is full-page images. PostgreSQL logs an entire 8 KB page the first time it's touched after a checkpoint, to survive torn writes. InnoDB doesn't put full pages in the redo log at all; it protects against torn writes with a separate **doublewrite buffer** (confirmed `ON` above), where pages are first written to a scratch area and then to their real location. So InnoDB's redo log only carries the logical change, which is much smaller. The two engines solve the same torn-page problem in completely different places, and it shows up directly in the log volume.

`SHOW ENGINE INNODB STATUS` is also where you'd watch the checkpoint age (how far the last checkpoint lags the current LSN); InnoDB throttles writes if that gap approaches the redo capacity, which is the analogue of PostgreSQL's `max_wal_size` pressure.

---

## 6. Undo log and MVCC

This is where InnoDB and PostgreSQL differ most. PostgreSQL keeps old row versions **in the table** (a new tuple per update, cleaned up later by VACUUM). InnoDB updates the row **in place** and pushes the _old_ version into a separate **undo log**. The `DB_ROLL_PTR` hidden column on each row points back to its previous version in the undo log, forming a chain.

That chain does two jobs at once, which is the answer to "why does InnoDB need both undo and redo":

- **Redo** replays committed changes after a crash (durability, roll _forward_).
- **Undo** holds old versions so a transaction can be rolled _back_, and so other transactions can read a consistent older snapshot.

I demonstrated the snapshot part directly. Under REPEATABLE READ, two sessions, one updating a row the other is reading:

```
Session A (REPEATABLE READ)        Session B
BEGIN;
read order 1  -> "ORIGINAL"
                                   UPDATE order 1 -> "CHANGED_BY_B"; COMMIT;
read order 1  -> "ORIGINAL"   <-- still sees the old value
COMMIT;
read order 1  -> "CHANGED_BY_B"
```

**Observation:** Even though B committed a new value, A's second read still returned `ORIGINAL`. A's consistent-read view was fixed when its transaction started, and InnoDB rebuilt the old version of the row by following `DB_ROLL_PTR` into the undo log. A only sees the new value after committing and starting a fresh read. This is exactly the same _outcome_ as PostgreSQL's snapshot isolation, but the mechanism is reversed: PostgreSQL reads an old tuple that's still in the table, InnoDB reconstructs an old version from undo.

The cost of in-place updates is the **purge** process: once no transaction can still need an old version, a background thread deletes those undo records. It's InnoDB's version of VACUUM, but it works on the (usually small) undo log rather than scanning the whole table. A long-running transaction holds the undo back and makes it grow, the same way it would hold back VACUUM in PostgreSQL.

There are two dedicated undo tablespaces:

```sql
SELECT TABLESPACE_NAME, FILE_NAME FROM information_schema.files WHERE FILE_TYPE='UNDO LOG';
 innodb_undo_001 | ./undo_001
 innodb_undo_002 | ./undo_002
```

---

## 7. Row locking and gap locks

InnoDB does row-level locking, and under REPEATABLE READ it also takes **gap locks** to stop phantom rows. I inspected the actual locks held using `performance_schema.data_locks`.

### A range lock takes next-key locks

```sql
BEGIN;
SELECT order_id FROM orders WHERE order_id BETWEEN 100 AND 105 FOR UPDATE;
SELECT index_name, lock_type, lock_mode, lock_data FROM performance_schema.data_locks;
```

```
 TABLE   | IX              | (whole table, intention lock)
 RECORD  | X               | 101     <- next-key lock (the record + the gap before it)
 RECORD  | X               | 102
 RECORD  | X               | 103
 RECORD  | X               | 104
 RECORD  | X               | 105
 RECORD  | X,REC_NOT_GAP   | 100     <- pure record lock on the boundary
```

### A single-row PK update takes only a record lock

```sql
BEGIN;
UPDATE orders SET status='pending' WHERE order_id = 250000;
```

```
 TABLE   | IX            | (intention lock)
 RECORD  | X,REC_NOT_GAP | 250000   <- just the one row, no gap
```

**Observation:** The lock modes tell the whole story. A plain `X` is a **next-key lock**: it locks the record _and_ the gap immediately before it. `X,REC_NOT_GAP` locks only the record. So the range scan locked records 101–105 together with the gaps between them, while the equality match on a unique PK (`order_id=250000`) only needed the single record, no gap. InnoDB is smart enough to skip gap locking when it knows the match is a unique single row.

### Gap locks actually block phantom inserts

To prove the gap lock does something, I used a small table with deliberate gaps (ids 10, 20, 30), locked the range in one session, and tried to insert into the gap from another:

```
Session A: BEGIN; SELECT * FROM gaps WHERE id BETWEEN 10 AND 30 FOR UPDATE;   (holds locks)
Session B: SET innodb_lock_wait_timeout=3;
           INSERT INTO gaps VALUES (25);
           -> ERROR 1205: Lock wait timeout exceeded
```

After Session A committed, the same `INSERT 25` succeeded immediately.

**Observation:** Id 25 doesn't exist, so there's no _row_ to lock, yet the insert was blocked. Session A's next-key lock covered the gap between 20 and 30, and an insert into that gap had to wait. This is how InnoDB prevents phantom reads under REPEATABLE READ: it locks the empty space, not just the rows. PostgreSQL achieves repeatable-read-without-phantoms differently (its snapshot simply never sees rows committed after the snapshot), which is why PostgreSQL doesn't have gap locks at all. Gap locks are a direct consequence of InnoDB choosing locking-based RR over pure snapshot RR.

---

## 8. The multi-table join

The same join from the PostgreSQL doc:

```sql
EXPLAIN ANALYZE
SELECT u.city, COUNT(*), SUM(o.amount) AS revenue
FROM users u JOIN orders o ON o.user_id=u.id
WHERE o.status='delivered' AND u.age BETWEEN 25 AND 35
GROUP BY u.city ORDER BY revenue DESC LIMIT 10;
```

Plan (trimmed):

```
-> Limit: 10 row(s)  (actual time=732..732 rows=5)
   -> Sort: revenue DESC
      -> Aggregate using temporary table
         -> Nested loop inner join  (cost=137242 rows=11071) (actual rows=43556)
            -> Filter: o.status='delivered'  (actual rows=236856)
               -> Table scan on o  (actual rows=1e6)
            -> Single-row index lookup on u using PRIMARY (id=o.user_id)
               (actual time=0.0017..0.0017 rows=1 loops=236856)
Execution time: 732 ms
```

**Observation:** This plan is completely different from PostgreSQL's, and the difference is instructive:

- **InnoDB chose a nested-loop join; PostgreSQL chose a hash join.** InnoDB scans `orders`, keeps the ~236,000 `delivered` rows, and for each one does a **single-row clustered-index lookup** on `users` by primary key. Each lookup took ~0.0017 ms because it's a clustered-index point lookup, which lands directly on the full row with no second fetch. That's the clustered index advantage in action: a PK lookup is as cheap as a lookup gets.
- **InnoDB ran it single-threaded (732 ms); PostgreSQL ran it parallel across 3 workers (88 ms).** Community MySQL 8.0 doesn't parallelize this style of query, so even though each individual lookup is fast, doing 236,000 of them in series is slower than PostgreSQL hashing the two tables in parallel. This isn't InnoDB being "worse", it's a planner/execution choice: nested-loop with cheap PK lookups is great for selective joins and OLTP point queries, less so for large analytical aggregations, which is the workload PostgreSQL's parallel hash join targets.

One more contrast, on the row estimates:

```sql
information_schema.tables ->  users: 523,187   orders: 997,667   (estimated)
SELECT COUNT(*)          ->  users: 500,000   orders: 1,000,000  (exact)
```

InnoDB's row counts in `information_schema` are estimates derived from sampling a few index pages (`innodb_stats_persistent_sample_pages`), so they drift a few percent from reality. PostgreSQL's `ANALYZE` statistics were closer. Neither is wrong, they're both samples, but it's a reminder that the optimizer is always working from approximations.

---

## 9. Trade-offs

**Clustered index.** Primary-key lookups and primary-key range scans are excellent: the data is right there in the index leaf, no second fetch, and rows with adjacent keys are physically adjacent on disk. The costs are real though: secondary indexes need an extra clustered-index lookup for non-covered columns (section 3), the choice of primary key matters a lot (a wide or random PK like a UUID bloats every secondary index and scatters inserts), and the table is bigger on disk than a plain heap (68.6 MB vs 41 MB here).

**Undo + redo instead of versions-in-table.** In-place updates keep the table compact and don't leave dead rows scattered through it, so InnoDB doesn't suffer PostgreSQL-style heap bloat. The price is that every consistent read of a recently-changed row may have to walk the undo chain to rebuild the old version, and a long transaction makes the undo log grow until purge can catch up. PostgreSQL pays for MVCC with VACUUM and bloat; InnoDB pays with undo maintenance and purge.

**Locking-based REPEATABLE READ.** Gap and next-key locks give InnoDB phantom-free repeatable reads even for locking statements, which is strong for correctness. The cost is reduced concurrency, gap locks can block inserts into ranges that aren't even occupied, and they're a common source of deadlocks in write-heavy workloads. PostgreSQL avoids gap locks by leaning on its snapshot model, but its default isolation is only READ COMMITTED.

**Redo without full-page images.** Smaller redo log volume (4.4 MB vs 15 MB), at the cost of the doublewrite buffer, which writes data pages twice. PostgreSQL folds torn-page protection into the WAL via full-page images instead. Each engine pays for the same guarantee, just on a different line item.

---

## 10. What I took away

The clearest lesson was that InnoDB and PostgreSQL make almost exactly _opposite_ choices and end up at the same correctness guarantees. InnoDB clusters the table on its primary key; PostgreSQL uses an unordered heap. InnoDB updates in place and keeps old versions in undo; PostgreSQL writes new versions into the table and cleans up with VACUUM. InnoDB prevents phantoms with gap locks; PostgreSQL prevents them with snapshots. Studying them side by side made each design make more sense than either would alone.

The two numbers that stuck with me were both from running the identical workload on both engines. The same 50,000-row update wrote 15 MB of WAL in PostgreSQL but 4.4 MB of redo in InnoDB, purely because of where each engine handles torn pages. And the same join ran in 88 ms on PostgreSQL (parallel hash) versus 732 ms on InnoDB (single-threaded nested loop with PK lookups). Neither engine is simply faster; they're tuned for different shapes of query, and the plans show it.

Finally, the clustered index is the idea that ties the whole engine together. The reason secondary indexes carry the PK, the reason a PK join lookup is so cheap, the reason the table is bigger on disk, and the reason primary-key choice matters so much, all of it comes back to "the table is the primary-key B-tree." Once that clicked, the rest of InnoDB's behaviour followed from it.
