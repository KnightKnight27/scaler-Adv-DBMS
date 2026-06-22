# MiniDB — Design Decisions & Trade-offs

This document records the **key engineering decisions** behind MiniDB, the
alternatives we considered, and the consequences of each choice. It is written
in the spirit of Architecture Decision Records (ADRs) and is intended both as a
design reference and as preparation material for the viva, where we are expected
to explain and defend our trade-offs.

The README explains *what* each module does; this document explains *why* it is
built that way.

---

## ADR-1 — Fixed 4 KB pages

**Context.** All table data must be laid out on disk in fixed-size units so the
page manager can compute a byte offset (`page_id × PAGE_SIZE`) instead of
maintaining a map of variable-length records.

**Decision.** Every page is exactly **4096 bytes**, matching the typical OS
memory-page and filesystem-block size.

**Alternatives considered.**
- *Larger pages (e.g. 16 KB).* Fewer page reads for large scans, but more wasted
  space for small tables and more memory per buffer-pool frame.
- *Variable-length pages.* Better space utilisation, but offset arithmetic is
  lost and free-space management becomes complex.

**Consequences.** A read or write never straddles two OS pages, so there is no
torn-I/O at the page boundary. With an ~48-byte row we fit ~85 rows per page,
which is enough to exercise multi-page scans and buffer-pool eviction in the
demo. Space is wasted at the tail of partially filled pages — acceptable at this
scale.

---

## ADR-2 — Clock Sweep buffer-pool eviction (not LRU)

**Context.** The buffer pool holds a fixed 10 frames between the heap file and
disk and must choose a victim frame when full.

**Decision.** Use **Clock Sweep** (the second-chance approximation of LRU that
PostgreSQL uses): a circular hand decrements each frame's `usage_count` and
evicts the first unpinned frame that reaches 0.

**Alternatives considered.**
- *True LRU.* More accurate recency tracking, but every page access must update
  a sorted/linked structure — O(log n) or pointer surgery on the hot path.

**Consequences.** Eviction decisions are O(1) and the access path stays cheap
(just a counter bump). The cost is that Clock Sweep only *approximates* LRU, so
in adversarial access patterns it can evict a page slightly sooner than true LRU
would. For our workload the accuracy difference is negligible and the simplicity
is worth it.

---

## ADR-3 — B+ Tree index, kept in memory

**Context.** Point lookups and range scans on the primary key need to be faster
than a full heap scan.

**Decision.** A **B+ Tree** (minimum degree t=2, max 3 keys per node) with all
records in the leaves and leaves linked in a chain. The tree lives in memory and
is rebuilt from the heap file on startup.

**Alternatives considered.**
- *Plain B-Tree.* Stores records in internal nodes too, which bloats them and
  makes range scans awkward (no leaf chain to walk).
- *Hash index.* O(1) point lookups but no range-scan support.
- *On-disk persisted B+ Tree.* Survives restart without a rebuild, but requires
  serialising nodes to pages and a node-level buffer-pool integration — a large
  amount of extra machinery.

**Consequences.** Range scans are efficient (descend once, then walk the leaf
chain). Point lookups are O(log n). The in-memory choice keeps the index code
focused on the *algorithm* rather than on persistence; the trade-off is a
rebuild cost at startup and the index size being bounded by RAM. This is
recorded as a limitation in the README.

> **Note on Delete.** We deliberately omit B+ Tree rebalancing (borrow/merge) on
> delete. The key is removed from its leaf, which is still *correct* — `findLeaf`
> continues to route correctly — but the tree is not space-optimal after many
> deletes. We chose correctness + simplicity over space optimality at this scale.

---

## ADR-4 — Cost-based optimizer with two physical plans

**Context.** A query with a WHERE clause can be answered either by scanning every
page or by using the B+ Tree index. The wrong choice can be orders of magnitude
slower.

**Decision.** A **cost-based optimizer** that estimates selectivity from Catalog
statistics and picks **IndexScan** for equality / low-selectivity (<20%)
predicates on `id`, and **SeqScan** otherwise. For joins, the smaller table
drives the nested loop.

**Selectivity model.**
- `id = v` → `1 / distinct_count`
- `id > v` → `(max − v) / (max − min)`
- `AND` → `s1 × s2`, `OR` → `s1 + s2 − s1·s2`

**Alternatives considered.**
- *Rule-based optimizer* ("always use the index if one exists"). Simpler, but
  picks an index scan even when most rows match — where a sequential scan that
  reads each page once is actually cheaper.

**Consequences.** The optimizer makes the *demonstrably* better choice for both
high- and low-selectivity predicates, which is exactly what the assignment asks
us to show (choosing between table scan and index scan). The selectivity model
is a standard textbook estimator and assumes uniform value distribution — it can
mis-estimate on skewed data, but it is transparent and explainable.

---

## ADR-5 — Strict 2PL with waits-for deadlock detection

**Context.** Concurrent transactions must not corrupt data or observe each
other's uncommitted writes.

**Decision.** **Strict Two-Phase Locking**: shared/exclusive locks are acquired
during execution and all released together at commit/abort. This yields
**serializable** isolation. Deadlocks are handled by **detection** — a waits-for
graph is walked (DFS) on each conflicting request, and a cycle triggers
`DeadlockException`, aborting the requester.

**Alternatives considered.**
- *Deadlock prevention (e.g. wait-die / wound-wait).* Avoids cycles by aborting
  pre-emptively based on timestamps, but aborts transactions that would not
  actually have deadlocked.
- *Timeout-based detection.* Simpler, but picks a victim purely on time, which
  causes false positives under load.

**Consequences.** Releasing locks only at end-of-transaction is what makes the
schedule serializable (a transaction can never see another's uncommitted data).
Detection (rather than prevention) only aborts transactions that are *genuinely*
in a cycle. The cost is the bookkeeping of the waits-for graph on every
conflicting lock request. This same blocking behaviour is what the Track B MVCC
extension targets and improves on.

---

## ADR-6 — Redo-only Write-Ahead Logging

**Context.** A crash mid-operation must not leave the database in a state where a
committed transaction's effects are lost.

**Decision.** **Write-Ahead Logging**: append the log record *before* mutating
the page. Recovery is **redo-only** — pass 1 collects committed transaction IDs,
pass 2 re-applies every INSERT/DELETE belonging to a committed transaction.

**Alternatives considered.**
- *Full ARIES-style redo + undo.* Handles uncommitted changes already flushed to
  disk, but requires log sequence numbers, compensation log records, and a
  dirty-page table — far beyond the scope here.
- *No logging / shadow paging.* Simpler conceptually but harder to make crash-safe
  for in-place updates.

**Consequences.** The WAL rule guarantees that anything needed to reconstruct a
committed transaction is on disk before the data page changes, so **committed
work is never lost** — exactly the property the assignment requires us to
demonstrate. Because recovery is redo-only, we assume uncommitted changes were
not flushed to the data file before the crash; handling that case (undo) is
listed as a limitation. The log is plain text and human-readable, which makes the
crash/recovery demo easy to follow.

---

## ADR-7 — Track B (MVCC) as the extension

**Context.** Under strict 2PL (ADR-5), readers take shared locks and therefore
block whenever a writer holds an exclusive lock. In a read-heavy workload this is
the dominant bottleneck.

**Decision.** Implement **Multi-Version Concurrency Control** for the benchmark:
each row carries a version, writers bump a `committed_version` on commit, and
readers take a *snapshot* of the committed version when they begin and read that
version without locking. Writers still take exclusive locks (no lost updates).

**Why this track.** It is the most direct response to a limitation already
present in our core system — readers blocking on writers — so the extension is a
*measurable improvement to our own design* rather than a bolt-on.

**Consequences.** Readers never block on writers, giving ~26× higher read
throughput in our benchmark (writer holds the lock ~83% of the time, so 2PL
readers are blocked most of that window while MVCC readers are not). The cost in
a full system would be version storage and garbage collection of old versions;
our benchmark models the visibility/throughput effect rather than long-term
version retention.

---

## Summary table

| # | Decision | Chosen for | Main trade-off accepted |
|---|----------|-----------|--------------------------|
| 1 | 4 KB fixed pages | No torn I/O, simple offsets | Tail space waste |
| 2 | Clock Sweep eviction | O(1) eviction, cheap access path | Approximates (not exact) LRU |
| 3 | In-memory B+ Tree | Range scans + simple algorithm focus | Rebuilt on restart, RAM-bound |
| 4 | Cost-based optimizer | Correct scan choice both ways | Assumes uniform distribution |
| 5 | Strict 2PL + detection | Serializable isolation, abort only real deadlocks | Readers block on writers |
| 6 | Redo-only WAL | Committed work never lost; simple | No undo of uncommitted flushed pages |
| 7 | MVCC extension | Removes reader/writer blocking | Version storage / GC not modelled |
