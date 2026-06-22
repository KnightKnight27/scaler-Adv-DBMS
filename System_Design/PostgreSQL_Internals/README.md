# PostgreSQL Internals

- **Role Number:** 24BCS10345
- **Name:** Ansh Mahajan

PostgreSQL stores tables as **unordered heaps** and keeps indexes as *separate*
structures that point into those heaps. Its defining choice is **MVCC where old
row versions stay in the table itself** — the opposite of InnoDB's undo-log
approach. That one decision explains the heap layout, the visibility rules, and
why `VACUUM` exists.

## 1. Files, pages, and the heap

Each table and index is one or more files of **8 KB pages** (the `block_size`
measured in Lab 2). A heap page is slotted, much like the SQLite page dissected
in Lab 4:

```
8 KB heap page
 ┌───────────────────────────────────────────────┐
 │ PageHeader (LSN, free-space pointers, flags)    │
 ├───────────────────────────────────────────────┤
 │ ItemId array  → → →   (line pointers, grow down)│
 │                                                 │
 │            (free space)                         │
 │                                                 │
 │   ← ← ←  tuples (heap rows, grow up)            │
 └───────────────────────────────────────────────┘
```

Line pointers (the `ItemId` array) give each tuple a stable address
`(page, slot)` called a **CTID**, so a tuple can move within reorganization
without every index needing an update (indexes can point at the line pointer).

## 2. Tuple visibility: xmin / xmax

Every row version (tuple) carries a header with:

- `xmin` — the transaction id that **created** this version.
- `xmax` — the transaction id that **deleted/superseded** it (0 if still live).

A tuple is visible to a transaction if `xmin` is committed and visible, and
`xmax` is not yet committed/visible. An `UPDATE` is therefore *delete + insert*:
it sets `xmax` on the old tuple and writes a **new tuple** elsewhere in the
heap. **Both versions physically coexist** until cleanup.

```
UPDATE row 'A' from v1 to v2:
   old tuple:  xmin=10  xmax=25   (now dead to new transactions)
   new tuple:  xmin=25  xmax=0    (the live version)
```

Compare **Lab 8**: there, versions were appended to an explicit chain. Postgres
does the same logically, but the chain lives in the heap and the bookkeeping is
the xmin/xmax pair plus the commit log (`pg_xact`/CLOG) that says which
transaction ids actually committed.

## 3. Write-Ahead Log (WAL)

Before any data page change reaches disk, the change is recorded in the **WAL**
(`pg_wal/`). On commit, the WAL record is flushed (`fsync`), the dirty page is
flushed later by the background writer/checkpointer. This is the same WAL
principle InnoDB uses with its redo log; it is what makes a commit durable
without forcing every touched page to disk immediately, and what enables
crash recovery, point-in-time recovery, and streaming replication.

## 4. VACUUM — the price of heap MVCC

Because dead tuples accumulate in the heap, PostgreSQL needs a garbage
collector:

- **VACUUM** reclaims space used by dead tuples and marks it reusable; it also
  updates the **visibility map** and **free space map**.
- **Autovacuum** runs it automatically based on churn.
- **VACUUM FREEZE** addresses *transaction-id wraparound*: xids are 32-bit, so
  very old tuples must be "frozen" (marked permanently visible) before the
  counter wraps.

Without vacuuming, an update-heavy table suffers **bloat**: the file keeps
growing with dead versions, hurting cache hit rates and scan times. This is the
explicit cost PostgreSQL pays for keeping reads lock-free.

## 5. Indexes and HOT

- The default index is a **B-tree** (Lipton/Lehman-Yao concurrent B+tree); also
  available: Hash, GiST, GIN, BRIN, SP-GiST.
- Indexes are **separate** from the heap and store the indexed key + CTID. Every
  index entry must be visited when a tuple's location changes — expensive.
- **HOT (Heap-Only Tuple)** updates optimize the common case: if an update does
  not change any indexed column *and* the new tuple fits on the same page, the
  new version is chained on the page and **no index entry is added**, sharply
  reducing index write amplification.

## 6. TOAST (oversized attributes)

A tuple must fit within an 8 KB page, so large values (big `text`/`bytea`) are
**TOAST**ed: compressed and/or stored out-of-line in an associated TOAST table,
with a pointer left in the main tuple. This keeps the main heap dense and fast
to scan even when a few columns are large.

## 7. Trade-offs

| Property | PostgreSQL's position |
| --- | --- |
| Read amplification | Low; reads never block writers and need no undo walk, but index→heap is a separate fetch. |
| Write amplification | Update = new tuple + index entries (mitigated by HOT); plus WAL. |
| Space amplification | Higher: dead tuples until VACUUM; needs background GC. |
| Concurrency | Excellent — readers don't block writers and vice-versa (snapshot MVCC). |
| Extensibility | Outstanding — pluggable index types, custom types, table-access methods. |

## 8. Observations / takeaways

- InnoDB and PostgreSQL both do MVCC, but the **location of old versions** is
  the key difference: InnoDB → undo log (current row stays slim), PostgreSQL →
  heap (needs VACUUM). Each pays somewhere.
- `VACUUM` is not optional housekeeping — on write-heavy systems it is core to
  keeping the database healthy, and tuning autovacuum is a real operational
  skill.
- HOT updates show how a targeted optimization (don't touch indexes when you
  don't have to) addresses the biggest cost of the heap-MVCC model.

## References

- *PostgreSQL Documentation* — "Database Physical Storage" and "Concurrency
  Control (MVCC)" chapters.
- H. Suzuki, *The Internals of PostgreSQL* (interdb.jp).
- B. Momjian, *MVCC Unmasked* (presentation).
