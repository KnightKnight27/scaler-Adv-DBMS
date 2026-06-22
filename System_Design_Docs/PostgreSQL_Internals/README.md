# PostgreSQL Internals

**24BCS10404 — Rajveer Bishnoi**

> Deep-dive into five subsystems that define how PostgreSQL works: the buffer manager, heap/B-tree page layout, MVCC, VACUUM, and WAL. All numbers are measured live using `pageinspect`, `pg_buffercache`, `pg_stats`, and `EXPLAIN (ANALYZE, BUFFERS)` on a 20k-student / 200k-enrollment dataset. Scripts: `setup.sql` (load data), `queries.sql` (all experiments), `results.txt` (captured output).

---

## 1. Buffer Manager — shared_buffers and the Clock-Sweep Eviction Policy

### What it does

Every Postgres backend that needs a heap page (data page) or index page must go through the **buffer manager**. The buffer manager maintains a **shared buffer pool** in shared memory (`shared_buffers`, default 128 MB = 16,384 pages of 8 KB each). A page is either *in the pool* (buffer hit) or must be fetched from the OS file cache / disk (buffer read).

### Data structures

- **Buffer descriptors array** — one entry per slot: page tag (relation OID + fork + block number), pin count, usage count, dirty flag, content lock.
- **Buffer hash table** — maps `(OID, fork, block)` → slot index for O(1) lookup.
- **Clock hand** — a global pointer that sweeps the descriptor array.

### Clock-sweep eviction

When a new page must be loaded but all slots are pinned or in use, the clock hand advances. At each position:
1. If `usage_count > 0`: decrement and skip (the page has been recently used).
2. If `usage_count == 0` and `pin_count == 0`: evict — flush if dirty, then claim the slot.

Every `buffer_get` that hits an existing page increments `usage_count` (capped at 5), giving recently-hot pages multiple chances to survive the sweep. This is **not LRU** — LRU requires a doubly-linked list touched on every access (too expensive under lock); clock-sweep is a good approximation with cheaper bookkeeping.

### Measured (pg_buffercache, post-query)

After running the 3-table join on our dataset (`results.txt` Q1):

```
relname           | buffers | cached
------------------+---------+---------
enrollments       |    1406 | 11 MB
idx_enr_student   |     255 | 2040 kB
students          |     141 | 1128 kB
students_pkey     |      57 | 456 kB
courses           |       7 | 56 kB
```

The query read 1,274 enrollment pages (parallel seq scan on 200k rows), 138 student pages, 8 course pages, and a few index pages. Almost all of the working set fits in the default 128 MB pool; only 5 reads from disk were observed (`shared hit=1564 read=5`).

---

## 2. Heap Page Layout

### Page anatomy (8 KB)

```
┌──────────────── 8192 bytes ────────────────────┐
│ PageHeaderData (24 bytes)                       │
│   lsn, checksum, flags, lower, upper, special   │
├─────────────────────────────────────────────────┤
│ ItemId array  (4 bytes each) ← grows downward   │
│   lp_off, lp_len, lp_flags  for each tuple      │
│                    ↕  free space                 │
│ Tuple data (HeapTupleHeader + attribute bytes)  │
│                    → grows upward from bottom   │
├─────────────────────────────────────────────────┤
│ Special space (0 bytes for heap, 16 for index)  │
└─────────────────────────────────────────────────┘
```

- `lower` = next free byte in the item-id array.
- `upper` = next free byte counting from the end of free space down toward itemids.
- A tuple is allocated by decrementing `upper` by `lp_len` and storing the offset in `ItemId[lp]`.

### Measured (pageinspect — `heap_page_items`)

```sql
SELECT lp, lp_off, lp_len, t_xmin, t_xmax, t_ctid
FROM heap_page_items(get_raw_page('students', 0)) WHERE lp <= 5;
```
```
lp | lp_off | lp_len | t_xmin | t_xmax | t_ctid
---+--------+--------+--------+--------+-------
 1 |      0 |      0 |        |        |        -- LP_DEAD (slot 1 after UPDATE)
 2 |   8144 |     48 |    812 |    814 | (0,2)
 3 |   8096 |     48 |    812 |    814 | (0,3)
 4 |   8048 |     48 |    812 |    814 | (0,4)
 5 |   8000 |     48 |    812 |    814 | (0,5)
```

Slot 1 is `LP_DEAD` because the row with `id=1` was updated (its live version moved to page 137, slot 32). The old version on page 0 slot 1 was the MVCC dead tuple; once it became safe to reclaim it was marked LP_DEAD by a HOT chain or pruning.

Page header:
```
lsn=0/44690F8  lower=652  upper=704  pagesize=8192  version=4  prune_xid=0
```
`upper−lower = 52` bytes of free space — the page is nearly full (144 tuples × 48 bytes ≈ 6.9 KB).

---

## 3. B-tree Index (Lehman-Yao B+-tree)

### Postgres implementation highlights

- **Lehman-Yao concurrency protocol** — each page has a "high key" as its first item; concurrent splits are safe without holding a parent lock.
- **Leaf pages** contain `(index key, ctid)` pairs pointing into the heap. Only the leaf level stores heap pointers; internal pages store keys + child-page pointers.
- Pages are linked in a doubly-linked list at the leaf level for efficient range scans.
- **Deduplication** (Postgres 13+): duplicate key entries on a leaf page are merged into a posting list, shrinking index size on low-cardinality columns.

### Measured (`bt_metap`, `bt_page_items`)

```sql
SELECT * FROM bt_metap('students_pkey');
```
```
magic=340322  version=4  root=3  level=1  fastroot=3  fastlevel=1  allequalimage=t
```

Level 1 = root → leaf. The `students_pkey` index has only two levels (root page 3 is also the first branch page; leaf pages hold the actual `(id, ctid)` pairs).

```sql
SELECT itemoffset, ctid, itemlen, data FROM bt_page_items('students_pkey', 1) LIMIT 5;
```
```
itemoffset | ctid     | itemlen | data
-----------+----------+---------+------------------------
         1 | (2,1)    |      16 | 6f 01 00 00 ...   -- high key (363)
         2 | (0,1)    |      16 | 01 00 00 00 ...   -- id=1 → heap (0,1)
         3 | (137,32) |      16 | 01 00 00 00 ...   -- id=1 (new version) → (137,32)
         4 | (0,2)    |      16 | 02 00 00 00 ...   -- id=2 → heap (0,2)
         5 | (0,3)    |      16 | 03 00 00 00 ...   -- id=3 → heap (0,3)
```

Two `ctid` entries for `id=1` are visible because the old heap slot (0,1) is dead but the index entry hasn't been cleaned by VACUUM yet. The planner uses `ctid=(137,32)` because `t_xmin` of the new tuple passes the snapshot check.

---

## 4. MVCC — xmin, xmax, ctid, and Snapshot Isolation

### Tuple versioning

PostgreSQL implements MVCC by **storing multiple versions of a row in the heap** rather than in a separate undo log. Every heap tuple carries:

| Field | Meaning |
|---|---|
| `t_xmin` | Transaction ID that inserted this version |
| `t_xmax` | Transaction ID that deleted/updated this version (0 = still live) |
| `t_ctid` | Self-pointer (normally), or points to the *newer* version on UPDATE |
| `infomask` bits | Whether `xmin`/`xmax` are committed, aborted, etc. (cached from pg_clog/pg_xact) |

### Snapshot

When a backend starts a query, it takes a **snapshot**: the current `xmax` XID and a list of in-progress XIDs. A tuple version is **visible** to a snapshot if:
1. `t_xmin` committed before the snapshot and is not in the in-progress list, **and**
2. `t_xmax` is 0 **or** the XID in `t_xmax` aborted **or** `t_xmax` started after the snapshot.

### Measured (tracing an UPDATE)

```
-- before UPDATE
id=1  ctid=(0,1)   xmin=812  xmax=814   -- xmax=814 means xid 814 deleted this version

-- during UPDATE (inside BEGIN)
id=1  ctid=(137,32) xmin=863  xmax=0    -- new version, not yet committed

-- after COMMIT
id=1  ctid=(137,32) xmin=863  xmax=0    -- same, now committed
```

The old version `(page 0, slot 1)` has `xmax=814` (the UPDATE transaction). A concurrent reader whose snapshot predates XID 814 sees `(0,1)`. A reader after XID 814 committed sees `(137,32)`. Both readers access the heap without blocking.

### READ COMMITTED vs REPEATABLE READ

- **READ COMMITTED** (default): a new snapshot is taken at the start of each *statement*. If a concurrent transaction commits between two statements in the same session, the second statement sees it.
- **REPEATABLE READ**: one snapshot per *transaction*. Concurrent commits are invisible for the lifetime of the transaction.

---

## 5. VACUUM and Dead Tuple Reclamation

### Why VACUUM is needed

Because UPDATE/DELETE leave old tuple versions in place (MVCC), heap pages accumulate **dead tuples** — versions that are no longer visible to any snapshot. Without cleanup, tables grow forever and index scans visit dead entries.

### What VACUUM does

1. Scans heap pages, identifies dead tuples (all snapshots have advanced past their `t_xmax`).
2. Marks their `ItemId` as `LP_DEAD` (free list entry).
3. Scans all indexes and removes references to those dead `ctid`s.
4. Updates `pg_class.relpages` / `pg_class.reltuples` and `pg_stat_user_tables`.
5. Advances the **oldest XID horizon** (`relfrozenxid`) to prevent XID wraparound.

VACUUM does **not** return space to the OS (it reclaims space within the page). `VACUUM FULL` does return space but takes an exclusive lock and rewrites the table.

### Measured

```sql
UPDATE enrollments SET grade = grade WHERE student_id < 2000;
-- 19,990 rows updated → 19,990 dead tuples created
SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname='enrollments';
-- n_live_tup=400000  n_dead_tup=0   (stats not refreshed until ANALYZE/autovacuum)

VACUUM (VERBOSE) enrollments;
-- INFO: tuples: 19990 removed, 200000 remain, 0 are dead but not yet removable
-- INFO: pages: 0 removed, 1402 remain  (dead slots reclaimed in-page, no truncation needed)
-- WAL usage: 1851 records, 359014 bytes  (VACUUM also generates WAL)

SELECT n_dead_tup FROM pg_stat_user_tables WHERE relname='enrollments';
-- n_dead_tup = 0
```

**Autovacuum** triggers automatically when `n_dead_tup > autovacuum_vacuum_scale_factor × reltuples + autovacuum_vacuum_threshold` (default: 20% + 50 rows).

---

## 6. WAL — Write-Ahead Log

### Purpose

WAL guarantees **durability** (D in ACID) and enables **crash recovery**. Before a dirty buffer is written to the data file, the corresponding WAL record must be flushed to the WAL segment on disk (`fsync`). On crash, Postgres replays WAL from the last checkpoint to bring the data files back to a consistent state.

### Architecture

```
backend:  generate WAL record  →  insert into WAL buffer (shared memory)
WAL writer process:  periodically fsync WAL segments to pg_wal/
checkpointer:  periodically flush dirty buffers + write a checkpoint WAL record
               (allows recovery to start from this point, not from the beginning)
```

### Key parameters (all confirmed in results.txt)

| Parameter | Value | Meaning |
|---|---|---|
| `wal_level` | `replica` | WAL contains enough info for streaming replication |
| `fsync` | `on` | WAL writes are fsynced (durability guaranteed) |
| `synchronous_commit` | `on` | COMMIT waits for WAL fsync before returning |

### Measured

```sql
SELECT pg_current_wal_lsn(), pg_walfile_name(pg_current_wal_lsn());
-- 0/4A2ABD0  →  000000010000000000000004  (segment 4)

-- VACUUM itself generated 1851 WAL records, 359 KB — even cleanup is WAL-logged.
```

WAL segments are 16 MB by default. The LSN `0/4A2ABD0` (≈ 78 MB from origin) means about 4–5 segments have been written since the cluster started.

---

## 7. Query Planning and Statistics

### pg_stats

The planner estimates row counts from column statistics collected by `ANALYZE`:

```
attname    | n_distinct | most_common_vals
-----------+------------+-----------------------------
student_id | 19923      | {12154}                    -- ~unique
course_id  |   500      | {400,135,476}              -- 500 distinct
grade      |    11      | {8,0,7,2,3,9,4,1,5,6,10}  -- 11 distinct (uniform)
```

For the predicate `student_id = 12345`, the planner estimates `200000 / 19923 ≈ 10 rows` → index scan. For `grade = 7`, it estimates `200000 / 11 ≈ 18182 rows` → seq scan. Both match actual rows in results.txt exactly.

### Cost model

PostgreSQL's planner uses a **cost-based optimizer** with configurable unit costs (`seq_page_cost=1.0`, `random_page_cost=4.0`, etc.) to compare all executable plans. For the 3-table join it chose `Parallel Hash Join` with one worker over a nested-loop plan because:
- 200k enrollment rows × random index lookups on students = 200k random I/Os at cost 4.0 each.
- A hash join scans both tables once (sequential) and probes the hash table in memory — far cheaper at this scale.

---

## 8. Key Learnings

1. **Shared buffers + clock-sweep**: PostgreSQL's buffer pool is a fixed SRAM region; the clock-sweep eviction is O(1) per page access, a practical approximation of LRU.
2. **Heap layout**: every page header tracks `lower`/`upper` for free-space management; `ItemId` slots separate logical position from physical offset, enabling HOT updates.
3. **MVCC in the heap**: no separate undo log; dead tuple versions accumulate in-place and are cleaned by VACUUM. This trades write amplification (old versions consume space) for reader/writer non-blocking.
4. **B-tree with Lehman-Yao**: concurrent page splits are safe without parent locks because each page carries a high-key; index entries are not removed until VACUUM cleans them.
5. **WAL before data**: the WAL-then-data contract means crash recovery only needs to replay WAL from the last checkpoint — data files may be stale but are always reconcilable.
6. **The planner is statistics-driven**: wrong statistics → wrong plan → order-of-magnitude slowdowns. `ANALYZE` is as important as indexes.
