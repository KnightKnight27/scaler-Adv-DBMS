# Design Decisions & Trade-offs

This document records the engineering choices we made and what we gave up. The
guiding principle was **correctness and clarity over feature count** — every
component is something each team member can explain.

### 1. Slotted pages with stable `RowId`s
A page stores a slot directory growing one way and tuple bytes growing the
other. A `RowId = (page, slot)` is an *indirection* through the slot, so an
index entry stays valid as long as the slot exists. We never compact a page, so
tuples never actually move; deletion just sets a slot's length to 0 (a
tombstone). **Trade-off:** deleted space inside a page is not reclaimed until
the page is rewritten — simpler code, slightly worse space utilisation.

### 2. Clock-sweep buffer eviction (not LRU)
Clock-sweep approximates LRU with a single reference counter per frame and an
O(1) amortised victim search, and it is more scan-resistant than strict LRU.
**Trade-off:** it is an approximation; a pathological access pattern can evict a
slightly-warmer page than true LRU would. For a teaching engine the simplicity
and O(1) cost win.

### 3. In-memory B+ tree, rebuilt on open
The B+ tree fully supports search / insert / delete / range scan, but its nodes
live in memory and are rebuilt by scanning the heap when a table opens.
**Trade-off:** we skip persisting index pages to disk (and the associated
recovery for them). Rebuild is O(N) per table at startup, which is fine at this
scale and keeps the index code focused on the algorithm rather than page I/O.
We also use **lazy deletion** (no leaf merge/rebalance on delete): search and
range scans stay correct; only fill factor degrades.

### 4. One shared buffer pool over a single data file
All tables share one `<db>.dat` file and one buffer pool; each table owns an
ordered list of its page ids, persisted in a small text catalog (`<db>.cat`).
**Trade-off:** a text catalog is easy to inspect and debug but is rewritten in
full on each DDL / page growth — negligible at this scale.

### 5. Row-level ARIES-lite recovery (no page LSNs / CLRs / checkpoints)
The WAL logs `BEGIN/INSERT/DELETE/COMMIT/ABORT` at *row* granularity. INSERT
carries the after-image (for REDO); DELETE carries the before-image (for UNDO).
Recovery REDOes committed ops and UNDOes losers, **idempotently keyed by primary
key** (insert-if-absent / delete-if-present), so it is correct regardless of
which heap pages happened to be flushed before the crash. **Trade-off:** no
fuzzy checkpoints means recovery scans the whole log; no per-page LSN means we
rely on idempotence instead of comparing versions. Much simpler, and provably
correct for our operations.

### 6. WAL flushed on every record
We `flush()` the log after each record rather than only at commit. This
guarantees the write-ahead rule even though heap pages are written lazily by the
buffer pool. **Trade-off:** more `fsync`-like flushes (slower writes) in
exchange for not having to coordinate buffer-pool eviction with log flushing.

### 7. Volcano (pull) executor
Every operator exposes `open()` / `next()`; parents pull from children one row
at a time. Base scans (`SeqScan`, `IndexScan`) and the join's inner side are
materialised for simplicity. **Trade-off:** materialising the inner relation of
a nested-loop join costs memory proportional to that relation — which is exactly
why the optimizer makes the *smaller* table the inner.

### 8. Heuristic cost-based optimizer
Selectivity uses standard heuristics (equality 0.1, range 0.33, AND multiplies,
OR by inclusion–exclusion), refined by the one exact fact we have: the primary
key is unique, so `pk = const` matches `1/N`. The optimizer only uses the index
for a *sargable* `pk = const` predicate reachable through `AND` (never under
`OR`), and always keeps a residual filter above an index scan, so **correctness
never depends on the estimate**. **Trade-off:** no histograms or multi-column
stats; estimates are rough but safe.

### 9. Track B — MVCC alongside 2PL, switchable
The concurrency engine implements both **MVCC** (versioned reads against a
begin-time snapshot, no read locks) and **strict 2PL** (shared/exclusive locks
released at commit). Writes take an exclusive lock in *both* modes; in MVCC,
commit additionally validates **first-committer-wins** (abort with `TxnConflict`
if a written key was committed by someone else after our snapshot), so lost
updates are impossible. Blocked requests are checked against a waits-for graph
and a cycle aborts the requester. **Trade-off / honesty:** MVCC here gives
*snapshot isolation*, which prevents lost updates and dirty/non-repeatable reads
but still permits **write-skew**; true serializable MVCC (SSI) is future work.
The engine is a standalone in-memory store (not wired into the on-disk heap) so
that the MVCC-vs-2PL comparison is clean and reproducible.
