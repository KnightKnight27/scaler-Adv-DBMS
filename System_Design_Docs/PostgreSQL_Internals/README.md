# PostgreSQL Internals — Buffer Manager, B-Tree, MVCC and WAL

**Name:** Pratyush Mohanty 
**Roll No.:** 24BCS10238 
**Data used:** the same `users` table from the SQLite comparison (500,000 rows), plus a generated `orders` table (1,000,000 rows) so I had something to join.

---

## 1. Setup

I reused the `users` table from the previous experiment and added an `orders` table referencing it, because the recommended exercise needs a multi-table join and a single table isn't enough to show the planner doing anything interesting.

```sql
CREATE TABLE orders (
    order_id   serial PRIMARY KEY,
    user_id    integer NOT NULL REFERENCES users(id),
    amount     numeric(10,2),
    status     text,
    order_date date
);
INSERT INTO orders (user_id, amount, status, order_date)
SELECT (random()*499999 + 1)::int,
       (random()*1000)::numeric(10,2),
       (ARRAY['pending','shipped','delivered','cancelled'])[(random()*3+1)::int],
       DATE '2023-01-01' + (random()*900)::int
FROM generate_series(1, 1000000);
ANALYZE users; ANALYZE orders;
```

To look inside pages and the buffer pool I enabled three contrib extensions that ship with PostgreSQL:

```sql
CREATE EXTENSION pg_buffercache;  -- see what's in shared_buffers
CREATE EXTENSION pageinspect;     -- read raw B-tree pages
CREATE EXTENSION pgstattuple;     -- measure dead space
```

Relevant defaults on this install:

| Setting | Value |
|---------|-------|
| `block_size` | 8192 (8 KB) |
| `shared_buffers` | 128 MB (16,384 page frames) |
| `wal_level` | replica |
| `checkpoint_timeout` | 5 min |
| `max_wal_size` | 1 GB |

---

## 2. How the pieces fit together

PostgreSQL runs as a set of processes around one shared-memory region. The previous doc already showed the 7 idle processes (postmaster, checkpointer, background writer, walwriter, autovacuum launcher, logical replication launcher, plus a client backend). What I wanted to understand here is what those processes actually do with memory and the files on disk.

```
   client ──connect──► postmaster ──forks──► backend (one per connection)
                                                  │
                              ┌───────────────────┴───────────────────┐
                              │            shared memory               │
                              │   shared_buffers        WAL buffers    │
                              └───────┬────────────────────┬───────────┘
              bgwriter / checkpointer │           walwriter │
                                      ▼                     ▼
                              base/ (heap + indexes)     pg_wal/
```

The one thing worth fixing in your head before reading the rest: when a row changes, the change goes into the WAL first and the actual data page is written later. A COMMIT waits for the WAL to hit disk, not for the data file. The data file gets updated in the background by the checkpointer and background writer. This is why the WAL section and the buffer section are really the same story told from two ends.

---

## 3. Buffer Manager

`src/backend/storage/buffer/`

`shared_buffers` is an array of 8 KB frames in shared memory. Every backend reads and writes pages through it. When a backend asks for a page, a hash table maps `(relation, block number)` to a frame. If the page is already there it's a hit; if not, it's a miss and the manager has to find a frame to put it in, evicting something if the pool is full.

The eviction policy is not LRU. It's a clock sweep: each frame has a `usage_count` between 0 and 5, a hand sweeps around decrementing counts, and the first unpinned frame it finds at 0 gets reused. Hot pages keep getting their count bumped back up so they stay; cold pages drift down to 0 and get evicted. It behaves roughly like LRU but without maintaining an ordered list on every single access, which matters when many backends hit the pool at once.

### Experiment: the same lookup cold, then warm

```sql
EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM users WHERE id = 250000;   -- run twice
```

| Run | Planning buffers | Planning time | Execution time |
|-----|------------------|---------------|----------------|
| First (cold) | `shared hit=80 read=4 dirtied=3` | 6.046 ms | 1.105 ms |
| Second (warm) | `shared hit=76` | 0.821 ms | 0.151 ms |

**Observation:** The first run read 4 pages from disk (`read=4`). The second run found everything already in `shared_buffers` (only `hit`), and planning time alone dropped from 6 ms to under 1 ms. The execution itself touched 4 buffers both times, which lines up with the B-tree being 3 levels deep (root + internal + leaf) plus the one heap page the index pointed to.

### Experiment: what's actually in the pool

```sql
SELECT c.relname, count(*) AS buffers, pg_size_pretty(count(*)*8192) AS buffered
FROM pg_buffercache b JOIN pg_class c ON b.relfilenode = pg_relation_filenode(c.oid)
GROUP BY c.relname ORDER BY buffers DESC;
```

```
   relname   | buffers | buffered
 orders      |  7358   |  57 MB
 users       |  5287   |  41 MB
 orders_pkey |  2099   |  16 MB
 users_pkey  |  1374   |  11 MB
```

**Observation:** The two tables and their primary-key indexes take up almost the entire 128 MB pool. The whole working set fit in memory, which is the reason every query after the first one ran warm. On a database much larger than RAM this is where the clock sweep would start mattering, because pages would actually get evicted.

---

## 4. B-Tree

`src/backend/access/nbtree/`

A B-tree page is 8 KB of sorted entries. Internal pages hold pointers (downlinks) to child pages; leaf pages hold pointers (`ctid`) into the heap. Leaves are also linked to their neighbours so a range scan can walk sideways without climbing back up.

### Experiment: the shape of the primary-key index

```sql
SELECT * FROM bt_metap('users_pkey');
```

```
 magic  | version | root | level | fastroot | fastlevel
 340322 |    4     | 412  |   2   |   412    |    2
```

`level = 2` means three levels: root at level 2, internal pages at level 1, leaves at level 0. So any of the 500,000 rows is reachable in 3 page accesses. The root page itself is almost empty:

```sql
SELECT live_items, free_size FROM bt_page_stats('users_pkey', 412);
 live_items = 5,  free_size = 8056
```

**Observation:** Five downlinks at the top fan out to the entire tree. A full leaf holds around 367 integer keys (measured below), so the fan-out per level is large enough that `log(500000)` only comes out to about 2.4, which is why the tree is just 3 levels. This is the whole reason a B-tree lookup stays cheap as the table grows.

### Experiment: forcing a page split

To actually see a split I made a small table and inserted 1,000 rows in order:

```sql
CREATE TABLE bt_demo (k int primary key);
INSERT INTO bt_demo SELECT generate_series(1, 1000);
```

```sql
SELECT blkno, type, live_items, free_size FROM ... ;   -- per page
 blkno | type | live_items | free_size
   1   |  l   |    367     |   808       leaf, full
   2   |  l   |    367     |   808       leaf, full
   3   |  r   |     3      |  8096       root, created by the split
   4   |  l   |    268     |  2788       last leaf, still filling
```

The root's downlinks show where the splits happened:

```sql
SELECT itemoffset, data FROM bt_page_items('bt_demo_pkey', 3);
 1 | (empty)      keys below 367
 2 | key = 367    keys 367 .. 732
 3 | key = 733    keys 733 and up
```

**Observation:** A leaf fills up at 367 keys. When the next key won't fit, the leaf splits in half, half the keys move to a new page, and a separator key gets pushed up to the parent. Here there was no parent yet, so the split created a brand-new root, and that's how the tree went from one level to two. Because I inserted in increasing order, only the last leaf is partially full; PostgreSQL has a fast path for appending to the rightmost page, which is the normal case for a `serial` primary key.

---

## 5. MVCC

`src/backend/access/heap/`

PostgreSQL never updates a row in place. Instead it writes a new version of the row and leaves the old one behind until it can be cleaned up. Every tuple carries two hidden columns to make this work:

- `xmin` — the transaction that created this version
- `xmax` — the transaction that deleted or replaced it (0 means still live)

A version is visible to a transaction if its `xmin` is committed and visible, and its `xmax` is not. That rule is the whole basis of snapshot isolation, and it's why a reader never has to wait for a writer.

### Experiment: watching an update create a new version

```sql
-- before
 ctid      | xmin | xmax |   id   |   name
 (2625,92) | 749  |  0   | 250000 | User250000

-- UPDATE users SET city='Bengaluru' WHERE id=250000;   (transaction 757)
 ctid      | xmin | xmax |   id   |   city
 (5285,53) | 757  |  0   | 250000 | Bengaluru
```

**Observation:** The row didn't change in place. The `ctid` moved from page 2625 to page 5285, meaning PostgreSQL wrote a completely new tuple somewhere else and set `xmax` on the old one to 757. The old version is now a dead tuple: invisible to new transactions but still sitting on disk.

```sql
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname='users';
 n_live_tup = 500000 | n_dead_tup = 1
```

### Experiment: why VACUUM exists

Dead tuples don't go away on their own. If you never cleaned them up, every update and delete would grow the table forever. `VACUUM` reclaims that space (and also advances the frozen-XID horizon, which prevents transaction-ID wraparound).

```sql
 n_dead_tup = 1
VACUUM users;
 n_dead_tup = 0
```

**Observation:** The single dead tuple from the update is gone and its space is reusable. In a busy system autovacuum does this automatically in the background. This is the direct cost of the no-overwrite design, and it's the main thing that's different from InnoDB, which keeps old versions in a separate undo log instead of leaving them in the table.

---

## 6. WAL

`src/backend/access/transam/`

The rule is in the name: the WAL record describing a change is written before the change counts as committed. WAL is one sequential stream, and writing sequentially is the fastest thing a disk can do, which is the whole point — it turns the durability cost of scattered random data-page writes into one cheap sequential write.

The write position is a Log Sequence Number (LSN):

```sql
SELECT pg_current_wal_lsn(), pg_walfile_name(pg_current_wal_lsn());
 0/14572EB8 | 000000010000000000000014
```

### Experiment: how much WAL an update generates

I recorded the LSN, ran a 50,000-row update, and recorded it again:

```sql
UPDATE orders SET status='shipped' WHERE order_id <= 50000;
 → 15 MB of WAL for 50,000 updated rows  (about 315 bytes per row)
```

**Observation:** 315 bytes to update a roughly 26-byte row is a lot, and the reason is full-page images. The first time a page is modified after a checkpoint, PostgreSQL logs the entire 8 KB page rather than just the change, to protect against torn writes (a page half-written when the machine crashes). `pg_stat_wal` confirms these are happening:

```sql
 wal_records | wal_bytes  | wal_fpi
   2624526   | 237405735  |  5913
```

**Crash recovery** uses this: on restart PostgreSQL goes to the last checkpoint and replays every WAL record after it, rebuilding any page changes that hadn't reached the data files yet. **Checkpointing** is what keeps that replay bounded: a checkpoint flushes all dirty buffers to disk and records that everything up to some LSN is safe, so recovery never has to go back further than the last checkpoint.

```sql
SHOW checkpoint_timeout; → 5min     SHOW max_wal_size; → 1GB
CHECKPOINT;   -- force one
```

A checkpoint fires on whichever comes first, the timeout or the WAL size limit. There's a real tension in tuning this, covered in section 8.

---

## 7. Query planning and pg_statistic

The planner is cost-based: it estimates how many rows each step will produce, then picks the cheapest plan. Those estimates come from statistics that `ANALYZE` collects into `pg_statistic` (readable through the `pg_stats` view).

```sql
SELECT attname, n_distinct, most_common_vals, most_common_freqs
FROM pg_stats WHERE tablename='orders' AND attname='status';
```

```
 status | 4 | {delivered,shipped,pending,cancelled} | {0.333,0.333,0.168,0.165}
```

So the planner knows `delivered` shows up about 33% of the time. Multiplying by the row count gives the estimate:

```
0.3335 × 1,000,000 ≈ 333,467 rows expected for status='delivered'
```

That number is computed from the histogram, not measured. The join below shows how close it lands.

### Experiment: the multi-table join

```sql
EXPLAIN (ANALYZE, BUFFERS)
SELECT u.city, count(*), sum(o.amount) AS revenue
FROM users u JOIN orders o ON o.user_id = u.id
WHERE o.status = 'delivered' AND u.age BETWEEN 25 AND 35
GROUP BY u.city ORDER BY revenue DESC LIMIT 10;
```

Plan, trimmed down:

```
Limit
└─ Sort  (key: revenue DESC)
   └─ Finalize GroupAggregate (group by u.city)
      └─ Gather Merge  (Workers Planned: 2, Launched: 2)
         └─ Partial HashAggregate
            └─ Parallel Hash Join  (o.user_id = u.id)   est 26354 → actual 19370
               ├─ Parallel Seq Scan on orders (status='delivered')
               │     est 145898 → actual 105233   removed 228100
               └─ Parallel Hash
                  └─ Parallel Seq Scan on users (age 25..35)
                        est 37632 → actual 30558   removed 136109
Planning Time: 1.344 ms
Execution Time: 87.904 ms
```

Three decisions worth pulling out:

**Sequential scans, not index scans.** Both filters match a large fraction of their table (a third of orders, about 18% of users), so scanning the whole table is cheaper than doing hundreds of thousands of random index lookups. The planner does use the index when it pays off, though:

```sql
SELECT * FROM orders WHERE order_id = 777777;   → Index Scan, 0.054 ms
SELECT count(*) FROM orders WHERE order_id < 900000;  → Parallel Seq Scan
```

A single-row lookup goes through the index; a range matching most of the table goes back to a sequential scan. Same table, opposite choice, driven entirely by selectivity.

**Hash join.** Both sides are large and unsorted, so PostgreSQL builds a hash table on the smaller side (users, 5,376 kB, fit in one batch with no spill) and streams orders past it. A nested loop would mean millions of index probes; a merge join would need both sides sorted first.

**Parallel workers.** The scans ran across 2 workers plus the leader. The `actual rows` figures on the scan nodes (105,233 and 30,558) are per-worker, and with `loops=3` the total `delivered` rows come to about 315k, against the estimate of 333k. That's roughly 5% off, which is good for a guess made from a histogram.

How the estimates held up overall:

| Step | Estimated | Actual | Off by |
|------|-----------|--------|--------|
| orders, status='delivered' | ~333,000 | ~315,700 | 5% |
| users, age 25..35 | 37,632 | 30,558 | 23% |
| join output | 26,354 | 19,370 | 36% |

The error grows as you go up the plan. Single-column statistics can't see that `age`, `city`, and whether a user has delivered orders are correlated, so the join estimate drifts further than either input. This is the practical reason multi-column (extended) statistics exist, and why a plan that was fine last month can go bad once the stats are stale.

---

## 8. Trade-offs

**MVCC.** Readers and writers never block each other, rollback is basically free (just don't make the new version visible), and consistent snapshots come for free. The price is dead tuples, which means bloat and a constant need for VACUUM, plus a tuple header on every row. InnoDB makes the opposite call: it updates in place and keeps old versions in undo logs, so its tables don't bloat the same way, but rollback and long-running read views cost more because it has to rebuild old versions from undo. Both pay for MVCC; they just pay in different places.

**WAL.** Writing the log sequentially and deferring the data-page writes is what makes commits fast. The cost is that every change is written twice (log now, data file later), and full-page images push WAL volume up further, as the 15 MB-for-50k-rows number showed. Checkpoint frequency is a genuine trade: checkpoint often and crash recovery is quick but you log more full-page images and get I/O spikes; checkpoint rarely and you write less but recovery has more WAL to replay. `synchronous_commit=off` is the escape valve when you can tolerate losing the last fraction of a second of commits in exchange for throughput.

**Buffer manager.** Clock sweep is cheaper than true LRU under concurrency, at the cost of being approximate. The ring buffer for big sequential scans is a deliberate choice to *not* cache a huge one-off scan, so it can't flush everyone else's hot pages out of the pool.

**8 KB pages** (versus SQLite's 4 KB) mean fewer pages for the same data and a shallower tree, which helps scans, at the cost of more wasted space on half-full pages and bigger full-page images in the WAL.

---

## 9. What I took away

The thing that surprised me most is how much follows from the single decision to never overwrite a row. The hidden `xmin`/`xmax` columns, dead tuples, VACUUM, autovacuum, the fact that an UPDATE physically moved my row to a different page — all of it is downstream of that one choice. You can't really understand any one of them on their own.

The other thing that stuck was how observable everything is. I expected the buffer cache and the B-tree to be black boxes, but `EXPLAIN (BUFFERS)` showed disk reads turning into cache hits on the second run, `pg_buffercache` showed exactly which tables were sitting in memory, and `pageinspect` let me watch a B-tree literally grow a level by splitting a full leaf at key 367. The page split went from being a diagram in a textbook to a row I could query.

And the planner is only as good as its statistics. A 5%-accurate single-table estimate became 36% off after one join, purely because single-column histograms can't see correlation between columns. That's a concrete reason stale statistics cause bad plans, and why `ANALYZE` matters as much as having the right indexes.
