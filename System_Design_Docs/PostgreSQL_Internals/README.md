# PostgreSQL Internal Architecture

**Piyush Bansal — 24BCS10079**

---

## 1. Problem Background

PostgreSQL is a database that many users read and write **at the same time**, and
it must never lose committed data — even if the server crashes mid-write. To do
this well it needs four cooperating parts:

1. A **Buffer Manager** so it doesn't read disk on every query.
2. A **B-Tree index** so it can find rows fast without scanning the whole table.
3. **MVCC** so concurrent users don't block or corrupt each other.
4. **WAL (Write-Ahead Logging)** so committed data survives a crash.

This document explains how these four fit together. (I also built simplified
versions of three of them in my labs — ClockSweep, B-Tree, and MVCC+2PL — so the
explanations are tied to code I actually wrote.)

---

## 2. Architecture Overview

```
        SQL query
           │
           ▼
   ┌─────────────────┐
   │ Parser / Planner│   chooses index-scan vs full-scan using statistics
   └────────┬────────┘
            ▼
   ┌─────────────────┐
   │    Executor     │
   └────────┬────────┘
            ▼
   ┌─────────────────────────────┐
   │   Buffer Manager            │  8 KB pages cached in shared_buffers
   │   (shared memory)           │
   └───────┬──────────────┬──────┘
           │ miss          │ every change first written here
           ▼               ▼
       data files       WAL (write-ahead log)
       (heap + index)   → guarantees durability + crash recovery
```

Pages live on disk in 8 KB blocks. The buffer manager keeps hot pages in memory.
Every change is recorded in the WAL *before* it touches the data files.

---

## 3. Internal Design

### 3.1 Buffer Manager (`src/backend/storage/buffer/`)

The buffer manager is a cache of disk pages in shared memory (`shared_buffers`).
When a query needs a page:
- **Hit** → page is already in memory, return it (fast).
- **Miss** → read it from disk into a free buffer slot. If no slot is free, **evict**
  one using the **Clock-Sweep** algorithm.

**Clock-Sweep** (which I implemented in Lab 3) approximates LRU cheaply: each buffer
has a usage counter. A "clock hand" sweeps the buffers; if a buffer's counter is >0
it decrements it and moves on, if it's 0 that buffer is evicted. This avoids the
cost of maintaining a strict LRU list while still keeping hot pages in memory.

### 3.2 B-Tree Index (`nbtree`)

PostgreSQL's default index is a B-Tree (I built one in Lab 4 / Lab 6). It keeps keys
**sorted** so it supports both equality (`= 5`) and range (`> 5 AND < 20`) queries.

- Each node is **one 8 KB page**, so one disk read fetches a whole node full of keys.
- High fan-out → the tree is **shallow** (often 3–4 levels for millions of rows),
  so a lookup is only a handful of page reads.
- **Insert** walks down to a leaf; if a node is full it **splits** and pushes the
  middle key up to the parent. This keeps the tree balanced.

### 3.3 MVCC — Multi-Version Concurrency Control

This is how PostgreSQL lets readers and writers run at once without locking
(I implemented this in Lab 8). Instead of overwriting a row, **every update writes a
new version** of the row. Each row version (tuple) carries two transaction IDs:

- **xmin** — the transaction that *created* this version.
- **xmax** — the transaction that *deleted/replaced* it (0 if still live).

**Visibility rule** — a version is visible to a transaction if:
- its `xmin` is committed and happened *before* this transaction's snapshot, **and**
- its `xmax` is 0, or not yet committed, or happened *after* the snapshot.

This gives **snapshot isolation**: each transaction sees a consistent picture of the
data as of when it started, even while others are changing it.

```
UPDATE balance 1000 -> 2000:

  [ value=1000, xmin=T1, xmax=T3 ]   <- old version, now marked deleted by T3
  [ value=2000, xmin=T3, xmax=0   ]   <- new version, live

  A reader that started BEFORE T3 committed still sees 1000.
```

**Why VACUUM is necessary:** because updates and deletes leave *dead* old versions
behind, the table grows with garbage. `VACUUM` reclaims space from versions no
running transaction can see anymore. Without it, tables bloat. This is the price
PostgreSQL pays for its append-style MVCC.

### 3.4 WAL — Write-Ahead Logging

**Rule:** write the change to the log *before* writing it to the data file.

When you commit, PostgreSQL only needs to make sure the WAL record is safely on
disk — not the actual data pages. The data pages get flushed later. So:
- **Durability:** if the server crashes, on restart it **replays the WAL** to
  re-apply any committed changes that hadn't reached the data files yet.
- **Checkpointing:** periodically PostgreSQL flushes dirty pages to the data files
  and marks a checkpoint, so crash recovery only needs to replay WAL *after* the
  last checkpoint (not the entire log).

This is also faster: WAL writes are **sequential** (cheap), while data-file writes
are **random** (expensive) and can be deferred and batched.

---

## 4. Design Trade-Offs

| Mechanism | Advantage | Cost / Trade-off |
|-----------|-----------|------------------|
| Buffer + Clock-Sweep | Avoids disk reads, cheap eviction | Approximate LRU, not exact |
| B-Tree index | Fast equality + range lookups | Must be maintained on every write; takes space |
| MVCC | Readers never block writers | Dead tuples accumulate → needs VACUUM |
| WAL | Durable + crash recovery + fast commits | Extra writes; log must be flushed on commit |

The big theme: PostgreSQL trades **extra background work** (VACUUM, checkpoints, WAL
flushing) for **never blocking concurrent users and never losing committed data**.

---

## 5. Experiments / Observations

The classic exercise is `EXPLAIN ANALYZE` on a join:

```sql
EXPLAIN ANALYZE
SELECT s.name, c.title
FROM students s
JOIN courses c ON c.id = s.course_id
WHERE s.gpa > 3.5;
```

What to look at in the output:
- **Chosen plan** — does it use an *Index Scan* or a *Seq Scan*? An index scan on
  `gpa` only happens if the planner thinks few rows match.
- **Planner estimates vs actual rows** — e.g. `rows=10` (estimate) vs `actual rows=8`.
  Big gaps mean stale statistics.
- **Statistics** — the planner's estimates come from `pg_statistic` (populated by
  `ANALYZE`). If stats are stale, the planner picks bad plans. This is *why*
  `VACUUM ANALYZE` matters for performance, not just space.

Observation: the planner is doing cost estimation, and its quality depends entirely
on the collected statistics.

---

## 6. Key Learnings

- The four subsystems work as a chain: **planner** picks the plan, **buffer manager**
  serves pages, **B-Tree** locates rows, **MVCC** keeps concurrent users isolated, and
  **WAL** keeps it all durable.
- **MVCC's hidden cost is VACUUM** — versioning is cheap to write but leaves garbage
  that must be cleaned up.
- **WAL is why a crash doesn't lose data**: log-first means recovery can always
  replay committed work.
- A database's performance is not just its data structures — it's also the
  **statistics** the planner relies on. Stale stats = bad plans.

---

### References
- PostgreSQL documentation — *Internals*: Buffer Manager, MVCC, WAL, Index Access Methods
- My Lab 3 (Clock-Sweep), Lab 4/6 (B-Tree), Lab 8 (MVCC + 2PL) implementations
