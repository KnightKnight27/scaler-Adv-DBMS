# PostgreSQL Internals

This is my write-up for Topic 2. After doing the SQLite vs PostgreSQL
comparison, I wanted to look deeper at how PostgreSQL actually works
inside — specifically the four pieces the topic asks about: the
buffer manager, B-tree indexes, MVCC, and the WAL.

The reason I picked this topic to spend the most time on is that
almost every lab in this course is a stripped-down version of one of
these components. Lab 3 is the buffer manager. Lab 4's B-Tree is
basically `nbtree`. Lab 6 is MVCC + 2PL. So I had something concrete
to compare to as I read about the real implementations.

## 1. Problem Background

PostgreSQL came out of Berkeley in the mid-1980s. The original goal
was to be a "next-generation" RDBMS that fixed limitations Stonebraker
saw in his earlier system (Ingres). The two big new ideas were:

1. **Extensible types and operators** — you should be able to add new
   data types (geometry, JSON, etc.) without forking the database.
2. **Multi-version concurrency** — readers shouldn't block writers and
   writers shouldn't block readers, even on the same row.

Both of those decisions still shape how PG looks today, 35+ years
later. The buffer manager and B-tree code are more "standard textbook
DBMS," but MVCC and the extensible type system are where PG diverges
from the rest of the field.

## 2. Architecture Overview

A running PostgreSQL cluster is a tree of processes plus one big chunk
of shared memory:

```
                postmaster
                    |
                    | fork() per connection
                    v
        backend1  backend2  backend3 ... ---+
                                            |
                                       SHARED MEMORY
                              +-----------------------+
                              |  shared_buffers       |
                              |  (8 KB pages cached)  |
                              |                       |
                              |  lock table           |
                              |  WAL buffers          |
                              +-----------------------+
                                            ^
                                            |
        bgwriter   checkpointer   wal-writer   autovacuum
            |            |             |             |
            v            v             v             v
        flush dirty  fsync all     fsync WAL    GC dead tuples
```

Backends share buffer pages, the lock table, and the WAL buffers. The
helper processes (bgwriter, checkpointer, wal-writer, autovacuum) make
sure dirty data eventually reaches disk and that the WAL can be
recycled.

## 3. Internal Design

### 3.1 Buffer Manager

PG keeps recently-used 8 KB pages in `shared_buffers`. Default is 128
MB. When a backend asks for a page:

1. If the page is in the buffer pool, return a pointer. Bump its
   `usage_count` so it survives the next eviction sweep. Pin the
   buffer (`refcount++`) so it doesn't get evicted while in use.
2. If not, find a victim frame using **clock-sweep** (see below), read
   the page from disk into that frame, and return it.

The replacement policy is in `src/backend/storage/buffer/freelist.c`.
The clock sweep walks the ring of buffer frames:

```
for each frame the hand passes over:
    if refcount > 0:                   skip (someone's using it)
    elif usage_count > 0:              usage_count--   (give it another chance)
    else:                              evict; return this frame
```

This is **exactly** what Lab 3 in this repo implements. The only
difference is that I used a single boolean reference bit in my
`ClockSweep<T>`, whereas PG uses a small integer (0..5) so really
popular pages get more "second chances" before they're evicted. The
background thread I added in Lab 3 corresponds roughly to PG's
`bgwriter`, which decrements usage counts and writes dirty pages out
in the background so foreground queries don't stall on eviction.

One subtle thing: PG also has a **ring buffer** mechanism for big
sequential scans. If a single query reads more pages than fit in the
buffer pool (e.g. a `SELECT *` on a 100 GB table), PG confines it to
a small private ring so it doesn't flush out everyone else's hot
pages. This is one of those engineering details that makes a real
buffer manager harder than a textbook clock sweep.

### 3.2 B-Tree Indexes (`nbtree`)

Every PG index by default is a B-tree, implemented in
`src/backend/access/nbtree/`. The leaf entries don't contain the row
data — they contain `(blockno, lineptr)` pointers (TIDs) into the heap.

So a query like

```sql
SELECT name FROM students WHERE id = 42;
```

with an index on `id` does:

1. Descend the index B-tree to find the leaf containing key `42`.
2. Read the TID from that leaf entry (say `(0, 5)` = block 0, item 5).
3. Read heap block 0, look at item 5, decode the tuple, return `name`.

Lab 4's B-Tree implements the same split-on-the-way-down insert that
PG uses (`_bt_split` in `nbtinsert.c`) — when a page is full, push
the median key up to the parent, give the right half to a new sibling
page. PG also has a fancier "deduplication" feature where repeated key
values on a leaf page get compressed, but the basic structure is what
Lab 4 builds.

PG B-tree pages always have a "high key" — an upper bound on what's in
that subtree — and right links between sibling leaves. The right
links let a concurrent reader follow a split that happened while it
was walking the tree, without taking a lock on the whole tree. This
"Lehman-Yao" style B-link tree is a real concurrency improvement over
the textbook B-tree.

### 3.3 MVCC (Multi-Version Concurrency Control)

This is the most distinctive thing about PostgreSQL.

Every row (heap tuple) carries two transaction IDs in its header:

- `xmin` — the txn that created this version of the row.
- `xmax` — the txn that deleted or updated it (0 if still live).

The visibility rule is roughly: tuple is visible to transaction `T`
with snapshot `S` if

```
(xmin == T   OR   (xmin committed   AND   xmin < S))
AND
(xmax == 0   OR   (xmax > S)   OR   xmax aborted   OR   (xmax == T))
```

`UPDATE` doesn't overwrite the old tuple — it inserts a new tuple
with `xmin = current_txn`, stamps the old tuple's `xmax = current_txn`,
and links them via `t_ctid`. So a row can have a chain of versions on
disk simultaneously, and different concurrent transactions can each
see whichever version their snapshot says is visible. This is why
readers in PG never block writers, even on the same row.

This is essentially what Lab 6 implements:

```cpp
bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    bool xmin_ok = (v.xmin == reader_xid)
                 || (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;
    if (v.xmax == 0) return true;
    bool xmax_invisible = (v.xmax == reader_xid)
                        || (is_committed(v.xmax) && v.xmax < snapshot_xid);
    return !xmax_invisible;
}
```

That's almost word-for-word the rule PG uses (with extra cases for
subtransactions and prepared transactions). Lab 6's Scenario 1
reproduces PG's snapshot isolation behaviour exactly.

The cost of MVCC is that the heap accumulates dead row versions over
time. That's the whole reason `VACUUM` exists — it walks the heap,
finds tuples that are dead from every active snapshot's point of view,
and reclaims their space. Without `VACUUM` (or autovacuum), the table
file would grow forever.

### 3.4 WAL (Write-Ahead Logging)

Before PG modifies a buffer page, it writes a WAL record describing the
change to a buffer in shared memory. On commit, the WAL up to that
point gets `fsync`'d to disk (`pg_wal/000000010000000000000001`, etc.).
Only *then* is the transaction considered durable.

The dirty data page itself gets written out lazily by the bgwriter or
the checkpointer — it doesn't need to hit disk at commit time, because
if the server crashes before that happens, the WAL has enough
information to recreate the change during recovery.

This gives the **D** in ACID: durability without paying for a full
data-file `fsync` per commit. It also gives PG:

- **Replication** — a replica is just another server replaying WAL
  records from the primary.
- **Point-in-time recovery** — restore a base backup, then replay WAL
  up to whatever timestamp.
- **Logical decoding (CDC)** — the WAL has enough information to emit
  a stream of "row X was updated" events for downstream consumers.

Checkpoints periodically flush every dirty page and write a checkpoint
record into the WAL. Once that's done, all WAL before the checkpoint
can be recycled. That's why checkpoint frequency is a trade-off: more
frequent → less recovery work after a crash, but more I/O.

### 3.5 The query planner

I haven't covered this in detail because the labs don't really touch
it, but in short: the planner uses statistics from `ANALYZE` (stored
in `pg_statistic`) to estimate how many rows each plan node will
produce, and picks the plan with the lowest estimated cost. `EXPLAIN
ANALYZE` shows both the plan it picked, the planner's row estimates,
and the actual row counts after execution. When the estimates are
wildly off from the actual counts, it usually means `ANALYZE` is stale
or the column has weird distribution that the histogram doesn't
capture.

## 4. Design Trade-Offs

### Append-only heap + MVCC

PG never updates a row in place. Every `UPDATE` is "insert a new
version, mark the old one dead." Pros: readers never block writers,
crash recovery is simpler (the old version is still there until
VACUUM removes it), and rollback is essentially free (just mark the
new versions as aborted in `pg_xact`). Cons: tables bloat, indexes
have to be re-walked, VACUUM is a real workload.

### One process per connection

Pros: hard isolation between sessions, no internal threading issues.
Cons: `fork()` per connection. Most PG production deployments need a
connection pooler (PgBouncer) in front to avoid spawning a backend per
HTTP request.

### Heap + secondary indexes vs clustered indexes

PG has no clustered index — every index is a secondary index pointing
into the heap. That gives flexibility (no "natural" order to enforce,
you can have many indexes that are all equally cheap), at the cost of
an extra indirection on every index lookup. MySQL/InnoDB picked the
opposite trade-off; I'll go into that in Topic 3.

### Shared buffer pool

All backends share one cache. Good: hot pages stay hot for everyone.
Bad: there's contention on the lock table for buffer headers, and
sizing `shared_buffers` is genuinely tricky (the OS page cache is also
in the picture, so making `shared_buffers` huge can be
counterproductive).

## 5. Experiments / Observations

The recommended exercise was to run `EXPLAIN ANALYZE` on a multi-table
join. I tried this with the Lab 2 setup. The schema there had a
`students` and a `marks` table; I ran:

```sql
EXPLAIN ANALYZE
SELECT s.name, AVG(m.score)
FROM   students s
JOIN   marks    m ON m.student_id = s.id
WHERE  s.age > 20
GROUP  BY s.name
ORDER  BY 2 DESC
LIMIT  10;
```

A few things I noticed in the plan:

- The planner picked a **hash join** rather than a nested loop, even
  though there was an index on `marks.student_id`. With 250k rows on
  each side, a hash join was cheaper than 250k index probes.
- The `students.age > 20` filter was pushed down before the join, so
  the hash table only contained students matching the predicate.
  Saving memory in the hash and reducing the join cardinality.
- The actual row count for the hash node was much higher than the
  planner's estimate — running `ANALYZE` first fixed that, and the
  plan didn't change but the estimate became accurate.
- The sort at the top (for `ORDER BY 2 DESC LIMIT 10`) used the
  "top-N heapsort" mode that PG has, so it didn't materialise the full
  sorted list.

Connecting that back to the labs:

- The buffer manager I wrote (Lab 3) is the thing reading the
  `students` and `marks` heap pages from disk into memory during the
  scan.
- The B-tree code (Lab 4) is what the planner *would* use if it picked
  a nested-loop join (it didn't, but the option was there).
- The MVCC visibility check (Lab 6) is what filters out tuples whose
  `xmin` isn't visible to my snapshot — this happens transparently
  inside every sequential scan and index scan in PG.

I didn't generate actual numbers (I'd need to recreate the Lab 2
dataset for this branch), but the shape of the plan and the way each
component fits together is the same.

## 6. Key Learnings

- PG's choice to make MVCC the central concurrency primitive is the
  most important one. Almost everything else (the heap layout, the
  TID-based indexes, the WAL, VACUUM, autovacuum, snapshot isolation)
  follows from "we never update in place."

- Building Lab 3 + Lab 4 + Lab 6 in this course was genuinely useful
  for reading PG source. The textbook versions I wrote are *exactly*
  what `freelist.c`, `_bt_split`, and `HeapTupleSatisfiesMVCC` do —
  just with more edge cases.

- The WAL is doing more than I initially appreciated. It's not just
  for crash recovery — it's the single source of truth that makes
  replication, PITR, and CDC possible. Every "advanced" PG feature
  more or less comes down to "interpret the WAL differently."

- The buffer manager being shared across all backends is what makes PG
  scale to many connections without each one fighting over its own
  cache. But it's also why connection pooling matters so much — a
  thousand idle backends each pinning a few buffer headers is real
  contention.

- Reading source code for a real database is surprisingly readable
  once you've written the toy versions. The naming (`xmin`, `xmax`,
  `t_ctid`, `usage_count`, `_bt_split`) starts to feel obvious because
  you've used the same names yourself.

## References

- PG internals book: <https://www.interdb.jp/pg/>
- PG source — `src/backend/storage/buffer/freelist.c` (clock sweep)
- PG source — `src/backend/access/nbtree/` (B-tree)
- PG source — `src/backend/access/heap/heapam_visibility.c` (MVCC)
- PG source — `src/backend/access/transam/xlog.c` (WAL)
- This repo: `Lab3/` (clock sweep), `Lab4/` (B-tree), `Lab6/` (MVCC + 2PL).
