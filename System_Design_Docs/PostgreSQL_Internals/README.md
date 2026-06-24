# PostgreSQL Internal Architecture

**Author:** Manjari Rathore
**Roll Number:** 23BCS10192
**Course:** Advanced DBMS — System Design Discussion

> This document goes *under the hood* of a single PostgreSQL backend and follows
> one page of data all the way through the engine: how the **buffer manager**
> hands it out, how the **B-tree (nbtree)** finds it, how **MVCC** decides
> whether a transaction may see it, and how **WAL** guarantees it survives a
> crash. Where I can, I anchor the explanation to code I actually wrote — the
> clock-sweep buffer pool from **Lab 3**, the B-tree from **Lab 6**, and the
> MVCC + locking transaction manager from **Lab 8** — and to the primary papers
> in my course resources (ARIES, Volcano). The query plans and timings come from
> my own **Lab 2** benchmark.

---

## 1. Problem Background

### Why PostgreSQL is built the way it is

PostgreSQL grew from the **POSTGRES** project at UC Berkeley, started by Michael
Stonebraker in 1986. The goal was a database that stayed correct with many users
at once and could be extended with custom types, operators, and index methods.
SQL support was added in 1994–96, and today's PostgreSQL keeps three core goals
from that original design:

1. **Many users at once, without corrupting each other.** This is what forces
   MVCC and a shared buffer pool.
2. **A committed transaction is never lost, even on a crash mid-write.** This is
   what forces write-ahead logging.
3. **Run arbitrary queries efficiently.** This forces a cost-based optimizer
   that uses statistics rather than executing SQL literally.

Everything in this document follows from those three goals. I covered the
comparison with SQLite in my `PostgreSQL_vs_SQLite` submission; this document
focuses on the internal machinery a single backend uses to meet those goals.

### The one-backend-per-connection model

A `postmaster` daemon listens on a socket and **forks one backend process** for
each client connection. All backends share one block of memory (`shared_buffers`)
plus background helpers (WAL writer, checkpointer, autovacuum, background writer).
This is the "process-per-connection" model described in Hellerstein, Stonebraker &
Hamilton's *Architecture of a Database System*. It is the context for everything
below: the buffer pool must be **shared and concurrency-safe** because many
backends access it at the same time.

---

## 2. Architecture Overview

```
   psql / app ──TCP or unix socket──►  postmaster (listener daemon)
                                          │ fork() one backend per connection
                                          ▼
   ┌───────────────────────────  one BACKEND process  ───────────────────────────┐
   │                                                                              │
   │   SQL text                                                                   │
   │      │ 1. Parser  (lex + grammar → parse tree)        ◄── Lab 7              │
   │      ▼ 2. Analyzer / Rewriter  (resolve names, apply views/rules)            │
   │   query tree                                                                 │
   │      │ 3. Planner / Optimizer  (cost-based; uses pg_statistic)               │
   │      ▼                                                                        │
   │   plan tree  ── 4. Executor (Volcano pull-model: open/next/close) ──┐        │
   │                                                                      │        │
   └──────────────────────────────────────────────────────────────────── │ ──────┘
                                                                          │ page requests
                       ┌──────────────────────────────────────────────────▼──────┐
                       │  shared_buffers  (shared page cache, clock-sweep)         │  ◄── Lab 3
                       └───────────┬──────────────────────────────┬───────────────┘
                          page miss │ read 8 KB                    │ dirty page
                                    ▼                              ▼ (WAL first!)
                  ┌──────────────────────────┐        ┌───────────────────────────┐
                  │ base/<db>/<rel> heap +    │        │  pg_wal/  (WAL segments,   │  ◄── ARIES
                  │ nbtree index files (8 KB) │        │  16 MB each, LSN-ordered)  │
                  └──────────────────────────┘        └───────────────────────────┘
        background helpers:  WAL writer · checkpointer · bgwriter · autovacuum
```

**Data flow for one query:** SQL → **parsed** into a tree (the recursive-descent
parser I built in Lab 7 is exactly this stage in miniature) → **planned** into a
tree of physical operators using statistics → the **executor** pulls tuples
through that tree one at a time → every page it touches is fetched through
`shared_buffers` → any change is written to **WAL before** the heap page is
allowed to reach disk. The five stages (parse → analyze → rewrite → plan →
execute) are PostgreSQL's actual top-level pipeline.

---

## 3. Internal Design

### 3.1 The Buffer Manager — `shared_buffers` (grounded in Lab 3)

PostgreSQL keeps its own user-space page cache, `shared_buffers` (default
128 MB, 8 KB pages), rather than relying on the OS cache alone, so it controls
eviction and can coordinate eviction with WAL. The cache is an array of fixed
slots; a hash table maps a **buffer tag** `(relation, fork, block#)` to a slot.
Each slot carries metadata: a **pin/reference count**, a **dirty** flag, and a
**`usage_count`**.

When `shared_buffers` is full, PostgreSQL picks a page to evict using the
**clock-sweep** algorithm — exactly what I built in Lab 3:

```cpp
// Lab 3 — ClockSweep.hpp : the eviction core
size_t sweep_for_victim() {
    while (true) {
        Frame& f = frames_[hand_];
        if (f.usage == 0) return hand_;     // cold slot → evict it
        f.usage--;                          // give it a "second chance"
        hand_ = (hand_ + 1) % capacity_;    // advance the circular hand
    }
}
```

A page request that **hits** bumps `usage_count` (capped); a **miss** runs the
sweep: the hand advances, decrementing each slot's counter, and evicts the first
slot that reaches 0. The cap matters — in my Lab 3 I defaulted `max_usage = 5`,
which is **exactly PostgreSQL's `BM_MAX_USAGE_COUNT = 5`**. The cap stops a
hot page from becoming un-evictable: even a page touched a million times needs at
most 5 sweep passes before it can be reclaimed.

Here is what my Lab 3 leaves out, which is also what the real buffer manager adds:

| Lab 3 (teaching) | Real `shared_buffers` adds |
|---|---|
| `usage` counter + clock hand | same idea, atomic counters |
| evict any zero-usage slot | **skip pinned slots** — a page being read by a live query can't be evicted (`pin_count > 0`) |
| overwrite victim in place | **flush dirty victims first** — and crucially, *flush their WAL first* (§3.4) |
| single-threaded | per-buffer spinlocks; a background **bgwriter** pre-cleans dirty pages so foreground evictions don't stall |

So the page-movement story is: *request → hashtable lookup → hit (pin, bump
usage) or miss (clock-sweep for a victim, flush it if dirty after its WAL is
durable, read the new 8 KB page in, pin it)*. That is the whole life of a page
through the buffer manager.

### 3.2 Index organization — the nbtree B-tree (grounded in Lab 6)

PostgreSQL stores a table as an **unordered heap** and builds indexes as
*separate* files using the `nbtree` access method — a **B-tree**. In Lab 6 I
implemented a classic B-tree with top-down preemptive splits:

```cpp
// Lab 6 — split a full child before descending into it
void insert(int key) {
    if ((int)root->keys.size() == 2 * t - 1) {  // root full → grow upward
        Node* newRoot = new Node(false);
        newRoot->kids.push_back(root);
        splitChild(newRoot, 0, root);
        root = newRoot;
    }
    insertNonFull(root, key);
}
```

That captures the essential B-tree invariants (keys between `t-1` and `2t-1`, a
node splits at the median, the tree grows from the root, search is a key-guided
descent). PostgreSQL's nbtree keeps those invariants but adds two production
details that my lab version deliberately skips, both for **concurrency**:

- **Lehman–Yao B-link tree.** Every page stores a **high key** and a
  **right-link** to its right sibling. nbtree splits **bottom-up** (on the way
  back up the descent), not top-down like my Lab 6. The right-link means a reader
  that lands on a page *mid-split* can simply follow the link rightward instead
  of holding locks down the whole path — so searches need only lightweight,
  short-lived page latches, not a locked root-to-leaf path.
- **Leaf pages are doubly linked**, so an ordered range scan (`WHERE x BETWEEN …`)
  walks leaf-to-leaf without re-descending the tree.

A lookup descends root → internal → leaf (the metapage at block 0 points to the
current root), then the leaf entry gives a **TID** `(block, offset)` into the
heap. So a PostgreSQL index lookup is *"walk the nbtree, then one extra fetch
into the heap"* — unless the query is covered by an **index-only scan**, where
the visibility map says the heap page is all-visible and the heap fetch is
skipped entirely.

```
nbtree:   metapage → root → internal → leaf ──TID(block,off)──► heap tuple
Lab 6:    root → … → leaf  (same descent; PG adds high-key + right-link)
```

### 3.3 MVCC — heap tuple versioning (grounded in Lab 8)

This is the heart of PostgreSQL concurrency, and it is exactly what I built in
Lab 8. Every heap tuple carries two hidden system columns:

- **`xmin`** — the transaction id that created this version.
- **`xmax`** — the transaction id that deleted/superseded it (0 = still live).

Writes never overwrite in place:

| SQL | Effect on the version chain |
|---|---|
| `INSERT` | append a new tuple `{value, xmin = me, xmax = 0}` |
| `UPDATE` | stamp the old visible tuple `xmax = me`, then append a new version |
| `DELETE` | stamp the visible tuple `xmax = me` |
| `SELECT` | walk versions, return the first one **visible** to my snapshot |

Each transaction reads against a **snapshot**, and a tuple's visibility is a
predicate over `xmin`/`xmax`. This is the actual function from my Lab 8:

```cpp
// Lab 8 — is this row version visible to a reader at snapshot_xid?
bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    bool xmin_ok = (v.xmin == reader_xid)                       // my own write, OR
                || (is_committed(v.xmin) && v.xmin < snapshot_xid); // committed before my snapshot
    if (!xmin_ok) return false;
    if (v.xmax == 0) return true;                               // still live → visible
    bool deletion_visible = (v.xmax == reader_xid)
                         || (is_committed(v.xmax) && v.xmax < snapshot_xid);
    return !deletion_visible;                                   // hidden only if its delete is visible to me
}
```

Real PostgreSQL stores this in the tuple header (`t_xmin`, `t_xmax`, `t_ctid`,
plus `infomask` *hint bits*), checks commit status in the **commit log**
(`pg_xact`/CLOG), and caches the answer in hint bits so it isn't re-checked. The
snapshot is `{xmin, xmax, in-progress xid list}` instead of my single integer,
but the predicate is the same shape. The defining payoff is what Lab 8 Scenario 1
demonstrates: **a reader that started before a concurrent update still sees the
old value — readers never block writers and writers never block readers.**

**The cost of MVCC — dead tuples and VACUUM.** Because updates and deletes only
*stamp* old versions, the heap accumulates **dead tuples**. `VACUUM` (and
`autovacuum`) reclaim that space and update the free space map; **freezing** very
old `xmin`s prevents 32-bit transaction-id **wraparound**. An optimization,
**HOT (heap-only tuples)**, lets an update that stays on the same page and
touches no indexed column skip creating new index entries — reducing index bloat.

### 3.4 Concurrency control — MVCC *and* locks (grounded in Lab 8)

MVCC removes read–write conflicts, but **write–write** conflicts still need
locks. PostgreSQL takes row-level locks for writers and, internally, short page
latches for structure. Lab 8 built this second half too — Strict 2PL with
deadlock detection:

- **SHARED** locks are mutually compatible; **EXCLUSIVE** conflicts with both.
- **Strict 2PL**: locks are acquired in the growing phase and all released
  together at commit/abort, which avoids cascading aborts.
- A **waits-for graph** is checked for cycles (DFS) on every blocked request; a
  cycle aborts the detecting transaction (Lab 8 Scenario 4).

This MVCC-over-locks layering is exactly how PostgreSQL combines snapshot reads
with serializable-enough writes. (My Lab 8 README has the full compatibility
matrix and the argument for why you need *both* mechanisms.)

### 3.5 Durability & recovery — WAL, grounded in ARIES

PostgreSQL's WAL is a textbook implementation of the **WAL protocol** from
Mohan et al.'s **ARIES** paper (one of my course resources). Two ARIES ideas map
directly onto PostgreSQL:

1. **The LSN and the WAL rule.** Every WAL record gets a monotonically
   increasing **Log Sequence Number (LSN)**, and *every data page stores the LSN
   of the last change applied to it* (`pd_lsn` in the page header — ARIES's
   "store the LSN in each page"). The protocol: **the WAL record describing a
   change must be on stable storage before the changed data page may be written
   back.** On `COMMIT`, the WAL is flushed (`fsync`) up to the commit record's
   LSN — ARIES calls this *forcing the log*. That flush *is* the durability
   guarantee, even while the heap page is still dirty in `shared_buffers`.

2. **In-place update + repeating history on restart.** Like ARIES (and unlike
   System R's shadow paging), PostgreSQL updates pages **in place** and relies on
   the log to recover. After a crash it starts from the last **checkpoint**'s
   redo pointer and **replays WAL forward** ("repeating history") to reconstruct
   every committed change. **Full-page writes** log a full image of a page on its
   first change after a checkpoint, so a torn (half-written) 8 KB page can be
   reconstructed.

```
  modify page → WAL record (LSN) to WAL buffer → fsync WAL at COMMIT  ◄── durability point
                                                     │
   (later) checkpointer flushes dirty heap pages, writes checkpoint w/ redo pointer
   (crash) redo WAL from last checkpoint's redo pointer forward  =  ARIES "repeating history"
```

**A contrast I only noticed after reading ARIES:** ARIES has
three passes — Analysis, **Redo**, **Undo**. PostgreSQL crash recovery is
essentially **redo-only** for table data. It does *not* need ARIES's physical
undo pass to remove an aborted transaction's row changes, because **MVCC already
makes them invisible**: an aborted `xmin` simply never becomes visible, and the
dead tuple is cleaned later by `VACUUM`. So PostgreSQL gets ARIES durability from
redo, while delegating "undo of aborts" to MVCC visibility + vacuum. (As we'll
see in my MySQL/InnoDB submission, InnoDB *does* keep an explicit ARIES-style
**undo** log, because it updates rows in place — a genuinely different choice.)

### 3.6 Query planning & execution — statistics, Volcano, parallelism

The planner is what makes PostgreSQL fast on the big joins where SQLite collapses
(see §5). It is **cost-based**: it enumerates plans (scan methods, join methods,
join orders) and picks the cheapest *estimated* one using statistics in
**`pg_statistic`**, gathered by `ANALYZE`:

- **`n_distinct`** — number of distinct values (drives join cardinality guesses)
- **most-common-values (MCV)** list + frequencies (drives selectivity of `=`)
- **histogram_bounds** (drives selectivity of ranges like `>=`)
- **correlation** between row order and column order (drives index-scan cost)

The chosen plan is a tree of physical operators executed by a **Volcano-style
pull model** — Goetz Graefe's *Volcano* paper (another course resource) is the
direct ancestor of PostgreSQL's executor. Every node implements an iterator
interface (`ExecInitNode` / `ExecProcNode` / `ExecEndNode` ≈ open/next/close); a
node pulls one tuple at a time from its children. Parallel query
(`Gather` + parallel workers) is exactly Volcano's *exchange operator* idea:
insert a parallelism operator into the tree without changing the other operators.

This is also where my Lab 7 work fits: the recursive-descent parser → AST is
PostgreSQL's parse stage, and the shunting-yard `WHERE`-clause evaluator I wrote
in Lab 7 is a tiny version of how an operator node evaluates a filter qualifier
per row.

---

## 4. Design Trade-Offs

**Advantages**
- **True concurrency** via MVCC: readers and writers don't block each other.
- **A sophisticated, parallel, cost-based optimizer** that wins decisively as
  query complexity grows (§5).
- **Strong durability** via ARIES-style WAL; the same log powers streaming
  replication and point-in-time recovery.
- **Extensibility** (custom types, index access methods, operators) — the
  original POSTGRES goal.

**Limitations / costs**
- **MVCC produces dead tuples** → `VACUUM` is mandatory background work; neglect
  it and you get bloat and wraparound risk. This is the price of never blocking
  readers.
- **Process-per-connection is heavy** — thousands of connections need a pooler
  (PgBouncer). Each backend is an OS process with its own memory.
- **WAL write amplification** — full-page writes after a checkpoint make the
  first change to each page expensive (the durability tax).
- **Planner depends on good statistics** — stale `ANALYZE` data → bad row
  estimates → bad plans. The optimizer is only as good as `pg_statistic`.

**The engineering decision in one line**

> PostgreSQL spends storage (dead tuples), background CPU (VACUUM), and per-page
> WAL overhead to buy *non-blocking concurrency and durability*. Those costs are
> not bugs — they are the bill for the three goals in §1.

---

## 5. Experiments / Observations

All numbers are from **my own Lab 2 benchmark** (same schema/data/queries across
PostgreSQL, MySQL, SQLite). Dataset: `customers` = 100k, `orders` = 500k,
`order_items` = 1.5M rows, indexed on the join/filter columns.

### Measured PostgreSQL timings (warm cache)

| Query | PostgreSQL | (SQLite) | (MySQL) | What the planner is doing |
|---|---|---|---|---|
| **Q1** filter + aggregate on `orders` | **31.6 ms** | 111 ms | 733 ms | index/seq scan + HashAggregate |
| **Q2** `customers ⋈ orders`, group by city | **71.9 ms** | 734 ms | 1,047 ms | **Hash Join** + HashAggregate |
| **Q3** `orders ⋈ order_items` (500k × 1.5M), top 10 | **549 ms** | timed out (>20 min) | 3,090 ms | **parallel Hash Join** + Top-N sort |

### Reading a plan (representative `EXPLAIN ANALYZE` for Q2)

The plan below is what PostgreSQL chose for Q2. The **71.9 ms** total is my Lab 2
result. The per-node figures show how PostgreSQL annotates a plan and the
*estimated-vs-actual* difference — I did not time each node separately.

```
HashAggregate  (group by c.city)                         actual rows=~1,000
  ->  Hash Join  (o.customer_id = c.customer_id)          est rows≈500k  actual=500k
        Hash Cond: (o.customer_id = c.customer_id)
        ->  Seq Scan on orders o                          500,000 rows  (the probe side)
        ->  Hash                                          builds hashtable on customers
              ->  Seq Scan on customers c                 100,000 rows  (the build side)
Planning Time:  ~0.4 ms
Execution Time: ~71.9 ms        ◄── measured in Lab 2
```

Three things this teaches, all tying back to §3.6:

1. **The planner chose a hash join, not a nested loop.** It builds a hash table
   on the smaller relation (`customers`, 100k) and probes it with `orders`
   (500k). That single decision is why PostgreSQL does Q2 in 72 ms while SQLite —
   which has no cost-based optimizer and falls back to nested loops — takes 734 ms
   and **could not finish Q3 at all**. The optimizer *is* the performance story.
2. **Estimated vs actual rows** is what `pg_statistic` drives. When they diverge
   badly (stale statistics), the planner picks the wrong join and the query slows
   down — the practical reason `ANALYZE`/autovacuum matters.
3. **Q3 goes parallel.** On the 500k × 1.5M join, PostgreSQL inserts a `Gather`
   over parallel workers (Volcano's exchange operator) — the 549 ms result is the
   parallel hash join doing what the client-server architecture was built for.

### Storage observation (from Lab 2)

PostgreSQL's database measured **260 MB** vs SQLite's 197 MB on identical data.
A real chunk of that gap is **MVCC bookkeeping** — `xmin`/`xmax` per tuple plus
accumulated dead versions before vacuum. The larger file is the *visible,
measurable cost* of the version chains I built in Lab 8.

---

## 6. Key Learnings

1. **A page's whole life is buffer manager → nbtree → MVCC → WAL.** Tracing one
   8 KB page through those four subsystems explains essentially all of
   PostgreSQL's behavior, and I had already built three of the four by hand in
   Labs 3, 6, and 8 without realizing they were the same machine.

2. **`BM_MAX_USAGE_COUNT = 5` is not arbitrary.** My Lab 3 clock-sweep used
   `max_usage = 5` and it turned out to be Postgres's real default. The cap is
   what stops hot pages from becoming permanently unevictable — a tiny constant
   with a real correctness purpose.

3. **MVCC is "append a version + a visibility predicate," and that's it.** The
   `is_visible(xmin, xmax, snapshot)` function from Lab 8 is the entire concept.
   Once you internalize it, snapshot isolation, dead tuples, and VACUUM all fall
   out as consequences.

4. **PostgreSQL recovery is redo-only because MVCC already handles undo.**
   Reading ARIES made this clear: PostgreSQL uses ARIES's redo + repeating-history
   + LSN-in-page, but skips the undo pass — aborted writes are simply *never
   visible* and get vacuumed later. InnoDB makes a different choice and keeps
   explicit undo logs.

5. **The optimizer is the difference, and the benchmark proves it.** The same Q3
   that PostgreSQL ran in 549 ms (parallel hash join) **never finished in SQLite**.
   The cost-based, statistics-driven, Volcano-style executor is exactly the
   component that justifies all the operational weight in §4.

6. **Every "overhead" is a deliberate purchase.** Dead tuples buy non-blocking
   reads; WAL full-page writes buy crash safety; per-connection backends buy
   isolation. PostgreSQL is a stack of paid-for trade-offs, not a free lunch.

---

## References

- C. Mohan, D. Haderle, B. Lindsay, H. Pirahesh, P. Schwarz, **"ARIES: A
  Transaction Recovery Method…"**, *ACM TODS* 17(1), 1992 — WAL protocol, LSN,
  repeating history, in-place update vs shadow paging. *(course resource:
  `Resources/aries.pdf`)*
- G. Graefe, **"Volcano — An Extensible and Parallel Query Evaluation System"**,
  *IEEE TKDE* 6(1), 1994 — the iterator (open/next/close) and exchange-operator
  model PostgreSQL's executor uses. *(course resource:
  `Resources/dace52a42c07f7f8348b08dc2b186061.pdf`)*
- J. Hellerstein, M. Stonebraker, J. Hamilton, **"Architecture of a Database
  System"**, *Foundations and Trends in Databases*, 2007 — process models, buffer
  pool, query processor. *(course resource: `Resources/fntdb07-architecture.pdf`)*
- Alex Petrov, ***Database Internals*** (O'Reilly, 2019) — B-trees, buffer
  management, MVCC, WAL. *(course resource: `Resources/Database Internals.pdf`)*
- PostgreSQL Documentation — *Internals: Buffer Manager, nbtree README, MVCC, WAL,
  Planner/Optimizer* — https://www.postgresql.org/docs/current/
- My own lab work: **Lab 2** (the PG/MySQL/SQLite benchmark + plans), **Lab 3**
  (clock-sweep buffer pool, `max_usage = 5`), **Lab 6** (B-tree with splits),
  **Lab 7** (recursive-descent parser → AST; shunting-yard `WHERE` evaluation),
  **Lab 8** (MVCC `xmin`/`xmax` visibility + Strict 2PL + deadlock detection).