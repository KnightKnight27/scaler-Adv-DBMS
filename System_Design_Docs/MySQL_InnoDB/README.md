# MySQL / InnoDB Storage Engine

**Author:** Manjari Rathore
**Roll Number:** 23BCS10192
**Course:** Advanced DBMS — System Design Discussion

> InnoDB is MySQL's default storage engine, and it makes choices that are almost
> the **opposite** of PostgreSQL's: the table **is** a clustered B+tree (not a heap),
> rows are updated **in place** (not appended), old versions live in a separate
> **undo log** (not in the table), and crash recovery does a full **redo + undo**
> pass (not redo-only). This document explains those choices and *why* they were
> made, using the B-tree from **Lab 6**, the buffer pool from **Lab 3**, the locking
> and deadlock detector from **Lab 8**, the real InnoDB numbers from my **Lab 2**
> benchmark, and the **ARIES** recovery paper.

---

## 1. Problem Background

### Why InnoDB exists

MySQL began with the **MyISAM** engine: fast, simple, table-level locking, and
**no transactions or crash recovery**. That was fine for read-mostly websites in
the early 2000s but unacceptable for anything handling money or concurrent
writers. **InnoDB** (Heikki Tuuri, Innobase Oy, integrated into MySQL and later
owned by Oracle) was built to give MySQL the missing pieces:

- **ACID transactions** with commit/rollback,
- **crash recovery** (a committed transaction survives `kill -9`),
- **row-level locking** so concurrent writers don't serialize on whole tables,
- **MVCC** so readers don't block writers.

Since MySQL 5.5 it is the default engine. InnoDB is designed for
**high-concurrency OLTP** — many short transactions doing lookups and small
updates by primary key — which is exactly the workload its clustered-index and
in-place-update design is built for.

### The pluggable-engine context

Uniquely, MySQL splits the **SQL layer** (parser, optimizer, connection handling)
from the **storage engine** (the part that actually stores rows, indexes, locks,
and logs). InnoDB plugs in underneath as one such engine. Everything in this
document is the storage-engine half; the parser/optimizer half lives in the MySQL
server above it.

---

## 2. Architecture Overview

```
        client ──► MySQL Server layer  (connection · parser · optimizer · cache)
                                │  handler API  (row-by-row: rnd_next / index_read …)
        ════════════════════════▼═══════════════════════════════════════════════
                         InnoDB STORAGE ENGINE
   ┌─────────────────────────── in memory ───────────────────────────────────┐
   │  Buffer Pool  (innodb_buffer_pool_size; 16 KB pages; LRU w/ midpoint)     │ ◄── Lab 3
   │  Change Buffer · Adaptive Hash Index · Log Buffer                         │
   └───────────┬───────────────────────────────┬───────────────┬─────────────┘
       page I/O│                       redo     │          undo │
               ▼                                ▼               ▼
   ┌────────────────────────┐   ┌──────────────────────┐  ┌──────────────────┐
   │  Tablespaces (.ibd)     │   │  Redo log (ib_logfile│  │  Undo logs /      │
   │  CLUSTERED B+TREE/table │   │  …): ARIES-style WAL, │  │  rollback segs:   │
   │  + secondary B+trees     │   │  LSN-ordered, fsync   │  │  old row versions │
   │  (16 KB pages)          │   │  at commit            │  │  for MVCC + undo  │
   └────────────────────────┘   └──────────────────────┘  └──────────────────┘
                                          ▲ ARIES (Mohan et al.)
```

**Data flow for a write:** the server layer parses/optimizes and calls down
through the *handler API* one row at a time → InnoDB locates the row in the
**clustered B+tree** (via the buffer pool) → takes a **row lock** → writes the
**undo** record (old image) and a **redo** record (how to redo the change) →
updates the row **in place** in the buffer pool → on `COMMIT`, flushes the redo
log. The data page itself can reach disk lazily; durability rides on the redo log
(the WAL protocol from ARIES).

---

## 3. Internal Design

### 3.1 Clustered index — the table *is* a B+tree (grounded in Lab 6)

This is the single most important InnoDB design choice. In InnoDB **the table is
stored as a B+tree keyed on the primary key**, and the **leaf nodes contain the
entire row**. There is no separate heap. (If you don't declare a primary key,
InnoDB picks the first non-null unique index, or invents a hidden 6-byte
`DB_ROW_ID`.)

My Lab 6 B-tree is the structure; InnoDB makes it a B+**tree** (all rows in the
leaves, internal nodes are pure routing, leaves linked for range scans):

```
Lab 6 B-tree (keys in all nodes)        InnoDB clustered index (B+tree)
        [ 32 | 64 ]                            [ 32 | 64 ]          internal = PK routing only
       /    |     \                           /     |      \
   leaves with keys                     ┌────────┐┌────────┐┌────────┐
                                        │PK + FULL ROW per leaf entry │  leaves hold the data
                                        └────────┘└───┬────┘└────────┘
                                         leaves doubly linked → fast PK range scan
```

**Why cluster?** A primary-key lookup is a *single* B+tree descent that ends
**on the row itself** — no second hop. Compare this to PostgreSQL (my
`PostgreSQL_Internals` doc), where an index scan finds a TID and then must fetch
the heap tuple separately. InnoDB folds those two steps into one. And because
rows are physically ordered by PK, a **PK range scan is sequential I/O**.

> Cross-link: this is the *same* idea as SQLite's rowid table from my
> `PostgreSQL_vs_SQLite` doc (Lab 4) — "the table is the index." InnoDB and SQLite
> both cluster; PostgreSQL keeps an unordered heap. Three real databases, two
> different answers to "where does the row physically live."

### 3.2 Secondary indexes — and why they do a *double* lookup

A secondary index is a separate B+tree, but its leaves **do not store a row
pointer** — they store the indexed column(s) **plus the primary key value**. So
looking a row up by a secondary key takes **two traversals**:

```
   secondary index B+tree ──(gives PK)──►  clustered B+tree ──► full row
   "find email = x"          PK = 4217       "find PK 4217"       the row
```

This is a real trade-off:

- **Cost:** every secondary-index lookup pays a second B+tree descent
  ("bookmark lookup"), and every secondary index physically **contains a copy of
  the PK** — so a *large* primary key (e.g. a UUID string) bloats *every*
  secondary index.
- **Benefit:** secondary indexes reference rows by **logical PK, not physical
  location**, so when a clustered leaf page splits and rows move, the secondary
  indexes need **no updating**. PostgreSQL's TIDs are physical, which is part of
  why it needs HOT chains and index maintenance on updates.

This also explains the famous InnoDB rule: **use a small, monotonic primary key
(`AUTO_INCREMENT`)**. A random PK (UUID) scatters inserts across the clustered
tree, causing constant page splits and fragmentation; an increasing PK appends to
the rightmost leaf cleanly.

### 3.3 Buffer pool — LRU with midpoint insertion (contrast with Lab 3)

InnoDB caches 16 KB pages in the **buffer pool** (`innodb_buffer_pool_size`). My
Lab 3 implemented **clock-sweep** with a `usage_count` (PostgreSQL's approach);
InnoDB uses an **LRU list with midpoint insertion** instead. Both solve the same
eviction problem, just in different ways:

| | Lab 3 / PostgreSQL: clock-sweep | InnoDB: midpoint-insertion LRU |
|---|---|---|
| Structure | circular array + usage counter | one LRU list split into **young** (≈5/8) and **old** (≈3/8) sublists |
| "Second chance" given by | `usage_count` must decay to 0 over sweep passes | a new page enters at the **midpoint**, not the head; it is only promoted to *young* if **re-accessed** after a short time window |
| Purpose of the trick | stop hot pages becoming unevictable (cap = 5) | stop a **full-table scan** from flushing the whole hot working set |

Both are "give a page a second chance before eviction"; InnoDB's specific worry
is *scan resistance* — one big `SELECT *` shouldn't evict the pages an OLTP
workload actually needs. InnoDB also adds a **change buffer** (defers secondary-
index maintenance for pages not in memory) and an **adaptive hash index** (builds
a hash shortcut over hot B+tree paths) — both pure performance, no new semantics.

### 3.4 MVCC via undo logs — "Oracle-style" versioning

InnoDB and PostgreSQL both give readers a consistent snapshot, but they store old
versions in **opposite places**:

| | PostgreSQL (my Lab 8 / Topic 2) | InnoDB |
|---|---|---|
| Update strategy | append a **new** tuple, mark old `xmax` | overwrite the row **in place** |
| Where old versions live | in the **table heap** (dead tuples) | in the **undo log** (rollback segment) |
| Per-row hidden fields | `xmin`, `xmax` | `DB_TRX_ID` (last writer), `DB_ROLL_PTR` (→ undo entry) |
| Reading an old snapshot | walk version chain in the heap | follow `DB_ROLL_PTR` **backward through undo** to rebuild the old version |
| Garbage collection | `VACUUM` / autovacuum | **purge** thread drops undo no read-view needs |

So InnoDB MVCC works like this: each clustered row has a `DB_TRX_ID` and a
`DB_ROLL_PTR`. A transaction's **read view** records which transaction ids are
visible. When it reads a row whose `DB_TRX_ID` is too new to see, it follows
`DB_ROLL_PTR` into the **undo log** and reconstructs the older version it is
*entitled* to see. My Lab 8 built the PostgreSQL flavor (versions in the heap);
InnoDB is the same visibility idea with the old versions relocated into undo.

**Why this matters:** because the table only ever holds the *current* version,
the clustered index stays compact and reads of current data are direct. The price
is that a **long-running read transaction** forces InnoDB to keep a long *history
list* of undo it cannot purge yet — InnoDB's analogue of PostgreSQL's bloat
problem, just relocated to the undo logs.

### 3.5 Undo logs *and* redo logs — why InnoDB needs both

The two logs protect **opposite directions in time**:

```
        UNDO log  ◄──────────────  ROW  ──────────────►  REDO log
   "how to take the change BACK"                  "how to put the change BACK"
   (before-image / old version)                   (after-image / how to redo)
   serves:  ROLLBACK on abort                      serves:  durability + crash redo
            MVCC old-version reads                          (the WAL protocol)
   = ATOMICITY + ISOLATION                          = DURABILITY
```

- **Redo log** = the WAL. It records *how to reapply* a committed change so that,
  after a crash, work that was committed but not yet flushed to the .ibd files is
  not lost. It is flushed (`fsync`) at commit (`innodb_flush_log_at_trx_commit=1`).
- **Undo log** = the before-images. It lets InnoDB **roll back** an aborted (or
  crash-interrupted) transaction, *and* serves MVCC reads (§3.4).

You need both because durability (don't lose committed work) and atomicity
(cleanly remove uncommitted work) are independent guarantees. Redo can't undo;
undo can't redo.

### 3.6 Recovery — the full ARIES three passes (grounded in `aries.pdf`)

InnoDB's redo log follows the **ARIES** design closely: every change gets a
**Log Sequence Number (LSN)**, each page stores the LSN of its last change,
updates happen **in place** (not shadow-paged), and InnoDB uses **fuzzy
checkpoints** so it never has to pause everything to flush to disk. Crash recovery
runs ARIES's passes:

```
1. ANALYSIS : scan redo forward from the last checkpoint → find dirty pages and
              which transactions were live at the crash.
2. REDO     : "repeat history" — replay redo from the checkpoint forward so the
              pages match their state at crash time (LSN in each page says what's
              already applied, so redo is idempotent).
3. UNDO     : roll back transactions that were still live (uncommitted) at the
              crash, using the UNDO logs.
```

**Key contrast with PostgreSQL:** in my `PostgreSQL_Internals` doc I noted
PostgreSQL recovery is effectively **redo-only** — it skips ARIES's undo pass
because MVCC makes an aborted transaction's rows simply *invisible*, to be
vacuumed later. InnoDB **cannot** skip undo: because it updated the row **in
place**, the only record of the pre-change value is in the undo log, so it *must*
run the undo pass to restore it. The "in-place update" choice is precisely what
forces InnoDB to keep — and replay — undo. This is a beautiful illustration of
how one design decision (clustered, in-place) cascades into another (mandatory
undo logging and an undo recovery pass).

### 3.7 Locking — rows, gaps, and next-key locks (grounded in Lab 8)

InnoDB does **row-level locking** on **index records** (not table-level like the
old MyISAM). The lock modes and the deadlock handling are exactly what I built in
Lab 8:

- **Shared (S)** and **Exclusive (X)** locks, with the same compatibility I coded
  (S–S compatible; X conflicts with both).
- **Two-phase locking** held to commit (Lab 8's Strict 2PL).
- **Deadlock detection** via a waits-for graph cycle check — *identical in spirit
  to Lab 8 Scenario 4*. The only difference: my lab aborts the transaction that
  *detects* the cycle, whereas InnoDB rolls back the **cheapest victim** (the
  transaction that modified the fewest rows) to minimize wasted work.

InnoDB adds one mechanism my lab didn't: **gap locks** and **next-key locks**.

```
   clustered/secondary index keys:    … 10 ───gap─── 20 ───gap─── 30 …
   a NEXT-KEY lock on 20  =  record lock on 20  +  gap lock on (10, 20]
```

A **gap lock** locks the *space between* index records so no other transaction
can `INSERT` into that range; a **next-key lock** = record lock + the gap before
it. This is how InnoDB's default isolation level, **REPEATABLE READ**, prevents
**phantom rows** (new rows appearing in a re-run range query) — something plain
2PL on existing rows can't stop. It's the lock-based answer to the same problem
MVCC snapshots solve for reads.

### 3.8 Isolation levels

InnoDB supports all four SQL levels; the default is **REPEATABLE READ**:

| Level | How InnoDB implements it |
|---|---|
| READ UNCOMMITTED | reads latest row, ignores read view (dirty reads possible) |
| READ COMMITTED | a **fresh read view per statement** (sees others' commits mid-txn) |
| **REPEATABLE READ** *(default)* | **one read view fixed at first read** + **next-key locks** on locking reads → no phantoms |
| SERIALIZABLE | as RR, but plain `SELECT`s implicitly take shared next-key locks |

---

## 4. Design Trade-Offs

**Advantages**
- **Clustered index** → PK lookups land on the row in one descent; PK range scans
  are sequential. Ideal for the OLTP "fetch/modify by primary key" workload.
- **In-place updates + undo** → the table stays compact (no in-table dead-version
  bloat), and reads of *current* data are direct.
- **Row + gap locking** → high write concurrency and phantom-free REPEATABLE READ.
- **ARIES redo+undo** → robust, well-understood crash recovery.

**Limitations**
- **Secondary indexes pay a double lookup** and embed the PK, so a large PK bloats
  every index — and a non-monotonic PK fragments the clustered tree.
- **Long-running reads grow the undo history list**, slowing purge and old-version
  reconstruction (InnoDB's version of "bloat", moved to undo).
- **Writes touch more logs** — every change writes both undo and redo. More
  durability machinery than an append-only design.
- As my Lab 2 benchmark shows (§5), MySQL **does not parallelize a single query**
  the way PostgreSQL does, so it trails badly on large analytical joins.

**InnoDB vs PostgreSQL — the trade-off in one line**

> PostgreSQL keeps old versions **in the table** and cleans up with VACUUM
> (cheap writes, table bloat, redo-only recovery). InnoDB keeps the current row
> **in place** and old versions **in undo** (compact table, mandatory undo +
> full redo/undo recovery, double secondary lookups). Same MVCC goal, opposite
> storage decisions — and all the other differences follow from that one choice.

---

## 5. Experiments / Observations

All figures are from **my own Lab 2 benchmark** (same schema/data/queries across
PostgreSQL, MySQL/InnoDB, SQLite). Dataset: `customers` = 100k, `orders` = 500k,
`order_items` = 1.5M rows.

### Measured InnoDB results

| Metric | InnoDB (MySQL) | PostgreSQL | SQLite |
|---|---|---|---|
| **Q1** filter + aggregate | 733 ms | **31.6 ms** | 111 ms |
| **Q2** join + group by city | 1,047 ms | **71.9 ms** | 734 ms |
| **Q3** 500k × 1.5M join, top 10 | 3,090 ms | **549 ms** | timed out |
| DB size on disk | **225.4 MB** | 260 MB | 197 MB |
| Page size | **16 KB** | 8 KB | 4 KB |
| Page count (derived) | ~14,425 | ~33,280 | 50,416 |
| Memory knob | `innodb_buffer_pool_size` | `shared_buffers` | `mmap_size`/OS cache |
| Storage layout | per-table `.ibd` tablespaces | heap + index files + WAL | single `.db` file |

### What the numbers actually say (architecture, not trivia)

- **InnoDB was 10–20× slower than PostgreSQL on every query.** This is *not*
  InnoDB being "bad" — it is the workload mismatch. InnoDB is tuned for
  **short OLTP transactions** (point lookups/updates by PK). My Lab 2 queries are
  **analytical** (big joins + aggregation), and MySQL executes them **row-by-row
  through the handler API with no intra-query parallelism**, while PostgreSQL
  throws a parallel hash join at them. The benchmark is measuring the gap between
  an OLTP engine and an analytical-capable one — exactly the architectural point.
- **InnoDB's file (225.4 MB) is smaller than PostgreSQL's (260 MB)** on identical
  data. A real reason: InnoDB updates **in place** and pushes old versions to
  **undo** (then purges them), so the main tablespace doesn't carry the dead
  tuples that inflate PostgreSQL's heap. The §3.4 design choice is visible in the
  file size.
- **InnoDB uses 16 KB pages — the largest of the three** (vs PG 8 KB, SQLite
  4 KB), so it stores the same data in the fewest pages (~14,425). Larger pages
  favor sequential clustered-index scans; they cost more on tiny random updates.
- The clustered-index win **does not show up in these analytical queries** — it
  shows up in PK point lookups, which this benchmark didn't isolate. Honest
  takeaway: my Lab 2 stresses the join optimizer, which is MySQL's weak spot, not
  InnoDB's clustered-index strength.

### Clustered vs heap lookup, made concrete (Lab 6)

Using my Lab 6 B-tree as the model: a `WHERE id = 4217` on an InnoDB table is one
descent ending **on the row** (the leaf holds the data). The same query in
PostgreSQL is a descent of the nbtree to a leaf holding a **TID**, then a separate
fetch of the heap page. For a point lookup, InnoDB does strictly less I/O — the
clustered design paying off.

---

## 6. Key Learnings

1. **"The table is a B+tree" explains most of InnoDB.** Clustered storage gives
   one-hop PK lookups and sequential PK scans, but forces double secondary-index
   lookups, PK-copying into every index, and the `AUTO_INCREMENT`-PK best
   practice. One decision, a whole family of consequences.

2. **Redo and undo are independent and you need both.** Redo protects committed
   work *forward* in time (durability); undo removes uncommitted work *backward*
   (atomicity) and reconstructs old MVCC reads. This made the "why both logs?" question make sense.

3. **In-place updates are *why* InnoDB must run ARIES's undo pass.** Reading the
   ARIES paper next to my Topic 2 work made it clear: PostgreSQL skips undo
   because MVCC hides aborts; InnoDB can't, because the old value only exists in
   undo. The recovery algorithm is dictated by the update strategy.

4. **The same MVCC, two storage locations.** My Lab 8 put old versions in the
   heap (PostgreSQL-style); InnoDB puts them in undo (Oracle-style). The
   visibility logic is identical — only the address of the old version differs,
   and that difference drives VACUUM vs purge, bloat vs history-list length.

5. **Gap/next-key locks are the lock-based cure for phantoms.** My Lab 8 locked
   existing rows; InnoDB also locks the *gaps between* them, which is the missing
   piece that makes REPEATABLE READ phantom-free. Locking ranges, not just rows.

6. **"Slower" is meaningless without the workload.** My Lab 2 made InnoDB look
   10–20× slower than PostgreSQL — but that benchmark is analytical joins, the
   opposite of InnoDB's OLTP sweet spot. The honest lesson is to match the engine
   to the workload, and to be skeptical of a single benchmark's verdict.

---

## References

- C. Mohan et al., **"ARIES: A Transaction Recovery Method…"**, *ACM TODS* 17(1),
  1992 — LSN, in-place update, fuzzy checkpoints, and the Analysis/Redo/Undo
  passes InnoDB recovery implements. *(course resource: `Resources/aries.pdf`)*
- J. Hellerstein, M. Stonebraker, J. Hamilton, **"Architecture of a Database
  System"**, *FnT Databases*, 2007 — buffer pool, transactions, storage-engine
  layering. *(course resource: `Resources/fntdb07-architecture.pdf`)*
- Alex Petrov, ***Database Internals*** (O'Reilly, 2019) — clustered vs heap
  storage, B+trees, undo/redo, MVCC. *(course resource:
  `Resources/Database Internals.pdf`)*
- MySQL Reference Manual — *InnoDB Storage Engine: Clustered/Secondary Indexes,
  Buffer Pool, Redo/Undo Logs, Locking, Transaction Isolation* —
  https://dev.mysql.com/doc/refman/8.0/en/innodb-storage-engine.html
- My own lab work: **Lab 2** (PG/MySQL/SQLite benchmark — the InnoDB numbers
  above), **Lab 3** (buffer-pool eviction; clock-sweep contrasted with InnoDB's
  midpoint LRU), **Lab 6** (the B-tree behind the clustered/secondary indexes),
  **Lab 8** (S/X row locks, Strict 2PL, waits-for deadlock detection — InnoDB's
  locking model).