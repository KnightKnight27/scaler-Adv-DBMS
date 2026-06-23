# MiniDB Design Notes and Trade-offs (Team XO)

This document records the key engineering decisions behind MiniDB and the
trade-offs each one represents. It complements the architecture overview in the
top-level `README.md`.

## Language: Go

Chosen for first-class concurrency primitives (goroutines, channels, `sync`),
`encoding/binary` for honest byte-level page layouts, a strong standard library,
and readable code that is easy to defend in a viva. We use the standard library
only - there are no third-party dependencies.

## Layering and dependency direction

The packages form a strict dependency hierarchy with `types` at the bottom and
`engine` at the top:

```
types <- storage <- index
types <- catalog
sql (standalone)
storage/index/catalog/sql <- planner
planner/catalog/storage/index/txn <- executor
storage <- txn
storage <- recovery
everything <- engine
```

To keep this acyclic, lower layers never import upper ones. The executor depends
on small interfaces (`executor.Table`, `executor.Tables`) that the `engine`
implements structurally, so the executor never imports `engine`. The same pattern
is used for `planner.Provider` and `recovery.Applier`.

## Storage: slotted pages, Clock buffer pool

- **Slotted pages** with a forward-growing slot directory and backward-growing
  tuple region support variable-length tuples and stable RIDs (deletes tombstone
  in place). Trade-off: deleted space is reclaimed only by reuse, not compaction.
- **Clock replacement** approximates LRU with O(1) bookkeeping and a single
  reference bit, which is simpler to implement correctly than true LRU and is
  what many real systems use.
- **STEAL / NO-FORCE** buffer policy. This is the interesting case for recovery:
  it requires both redo and undo. The alternative (NO-STEAL/FORCE) would make the
  WAL almost pointless, defeating the purpose of the recovery module.

## Indexing: in-memory B+Tree with composite ordering

- Entries are ordered by `(key, rid)`, making every entry unique even for
  duplicate keys. One implementation then serves unique PK indexes, non-unique
  secondary indexes, and MVCC version chains.
- **In-memory, rebuilt at startup.** This keeps the focus on the B+Tree algorithm
  (split/merge/redistribute) rather than on paging the tree to disk. The cost is
  that index durability relies on rebuilding from the WAL-recovered heap. A
  production system would log and page the tree.

## Execution: Volcano iterator model

Each operator pulls one row at a time via `Next()`. This makes the optimizer's
choices observable, composes cleanly (filter over scan, join over scans), and is
the model used by most textbook and many production engines. Base scans
materialize their locked/visible row set first to keep page latches short and
avoid blocking on a logical lock while pinning a page.

## Optimizer: cost-based, statistics-driven

Selectivity from distinct counts; scan choice and join algorithm/order by
estimated cost. Statistics are cached and recomputed lazily after writes so that
planning a point lookup does not itself cost a full scan. Trade-off: no
histograms, so estimates for skewed data are coarse.

## Concurrency: latches vs. locks

A crucial distinction we maintain deliberately:

- **Latches** are short physical mutexes (per-table `RWMutex`) protecting the
  heap and index during a single operation.
- **Locks** are logical, record-level, and held for the whole transaction (strict
  2PL).

Logical locks are never acquired while a latch is held, so latches never enter
transaction-level deadlocks and stay brief.

Deadlocks are detected eagerly with a wait-for graph, with a bounded
lock-acquisition timeout as a liveness backstop for cycles that complete among
already-waiting transactions.

## Extension: MVCC integrated into the heap

Rather than a separate in-memory store, MVCC versions live in the same heap pages
with an `(xmin, xmax)` header, and the existing duplicate-key B+Tree indexes the
versions. This keeps the extension integrated with the core storage engine.
Trade-off: no garbage collection of dead versions (documented limitation).

## Recovery: ARIES-flavoured, simplified

Three phases (analysis, redo, undo) with physical before/after images and idem
potent redo via exact-RID `PutAt`. Simplifications: no checkpoints (full log
replay) and no per-page LSN (redo applies after-images unconditionally, which is
idempotent). Both are reasonable for MiniDB's scale and are called out as
limitations.
