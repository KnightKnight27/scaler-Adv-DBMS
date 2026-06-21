# WALterDB — Design Decisions & Trade-offs

This document records the engineering decisions behind WALterDB and the
trade-offs each one makes — what is prioritized over what, and why the choice is
sound. WALterDB is a single-process relational database engine with an LSM-tree
storage extension (Track C).

---

## Design philosophy

Two priorities order every decision below:

1. **Correctness and explainability are prioritized over feature count and raw
   performance.** A smaller engine whose every component is provably correct and
   can be reasoned about is worth more than a larger one with hidden gaps. Each
   simplification is deliberate and bounded, not accidental.
2. **A single storage abstraction is prioritized over specialization.** The
   `StorageEngine` key-value interface (`src/engine/storage_engine.h`) is defined
   before either storage engine. Both the classic heap+B+tree engine and the
   LSM engine implement it, so the relational layer is written once against the
   abstraction and the Track-C comparison is an engine swap rather than a fork.
   The relational layer maps a row to a KV pair
   (`key = table_id ‖ primary_key`, `value = serialized tuple`), so all rows of a
   table form one contiguous, ordered key range.

---

## Storage layer

**Slotted pages** are used for the on-disk record layout: a header, a slot array
growing forward, and variable-length tuples growing backward from the end of a
fixed 4 KB page. This layout is chosen to support variable-length records with
stable addresses. A deleted record is tombstoned (its slot offset is set to 0)
and the slot id is never reused, so any record id (`RID`) held elsewhere — in the
B+tree, in a cursor — stays valid for the life of the page. The trade-off:
**stable addressing is prioritized over space reclamation.** Tombstoned bytes are
not compacted within a page, because compaction would have to rewrite every RID
pointing into that page; reclaiming that space is left to a full rewrite, which
the current scope does not require.

**One global page space in a single file** is chosen for the disk manager.
Keeping all heap and index pages in one page-id space prioritizes **recovery
simplicity** over physical isolation between tables: there is exactly one file
and one address space to reason about during redo.

**Heap files** thread their pages into a singly linked list and append new rows
to the tail page. This prioritizes **insert simplicity** over read locality and
space packing; a free-space map that backfills earlier pages is a known
alternative that is not required at this scale.

## Buffer pool

The replacement policy is **LRU-K with K = 2** rather than plain LRU. The reason
is scan resistance: a page touched fewer than K times is treated as having
infinite backward-distance and is evicted before a page that is genuinely hot, so
a one-shot sequential scan cannot flush the working set. On eviction the *current*
(dirty) version of a page is written back, never a stale copy. The pin/dirty
bookkeeping prioritizes **correctness of the flush ordering** over lock-free
speed: a single latch guards the pool's metadata.

## Indexing — B+tree

The B+tree uses **fixed-fanout nodes with a bounded key length (64 bytes)** rather
than fully variable-length slotted nodes. Fixed strides make the split and
redistribute logic obviously correct, which prioritizes **provable correctness of
the hardest index operation** over the space efficiency of packing
variable-length keys; the bound only limits very long string keys (integer keys
encode to 9 bytes).

**Delete is lazy: a key is removed from its leaf with no merge or rebalance on
underflow.** This is the single most consequential trade-off in the index. Full
B+tree deletion with merge/redistribute is the largest correctness risk in the
whole engine, and search and range-scan correctness do not depend on it.
Tolerating under-full nodes prioritizes **correctness and bounded complexity**
over optimal node occupancy — a technique real systems also use to defer
rebalancing work.

The current **root id lives in a stable meta page**, not in a variable that would
go stale across a root split or a reopen; the tree's identity is the meta page,
which is what the catalog stores.

## Relational types and catalog

Tuples use a **schema-driven byte encoding** (a null bitmap followed by each
non-null field) rather than a self-describing format with per-value type tags.
This prioritizes **compactness** over self-description: the schema is always
available at decode time, so tags would be redundant.

Column values encode to **order-preserving key bytes** (big-endian with a flipped
sign bit for integers and IEEE-754 doubles), so that lexicographic byte
comparison reproduces value order. This is what lets a column value be used
directly as a B+tree / KV key with correct range-scan ordering.

**DDL is persisted to a sidecar catalog file, rewritten atomically on each
schema change, and is not WAL-logged.** This prioritizes **simplicity** on a rare
path: schema changes are infrequent, and recovery targets row-level
INSERT/DELETE on existing tables rather than schema evolution.

## Query execution

The executor follows the **Volcano (iterator) model**: every operator implements
`open()/next()/close()` and one row flows through the whole tree per call at the
root. This pull-based model is chosen for its uniformity — operators compose
without materializing intermediate results, except where an algorithm requires it.

The only join algorithm is the **block nested-loop join** (the right input is
materialized once, then joined against the streaming left input). The required
feature is "JOIN" without a specified algorithm; nested-loop is chosen because it
is correct for any predicate and simple to reason about, prioritizing
**generality and clarity** over the throughput of sort-merge or hash joins at
large scale.

Column references are **resolved by name at evaluation time** against the
operator's result schema, which prioritizes **implementation simplicity** over the
speed of a pre-bound positional plan; query-evaluation speed is not on the
measured path.

## Optimizer

The optimizer is genuinely cost-based but deliberately lightweight:

- **Scan choice** compares a B+tree point lookup against a full scan: an equality
  predicate on the primary-key column yields an `IndexScan`, otherwise a
  `SeqScan`. This is the decision the cost model most visibly drives.
- **Cost** is estimated in page-I/O terms: a sequential scan ≈ heap pages, an
  index point lookup ≈ tree height + 1; selectivity for a primary-key equality is
  one row. Heuristic estimation is prioritized over statistics-heavy modelling
  because the decisions it must make are coarse.
- **Join order** is chosen greedily, smallest relation first, adding the smallest
  relation that an unapplied join predicate connects to the already-joined set.
  Greedy ordering is prioritized over exhaustive dynamic-programming enumeration,
  which is unnecessary at the join sizes in scope.

## Transactions and concurrency

Isolation uses **strict two-phase locking**: locks are acquired on demand and all
released together at commit or abort, which yields serializable schedules for the
locked items.

Lock **granularity is table-level**. A coarse granularity is prioritized over
maximum concurrency because it is far less bug-prone under time pressure, and it
yields a clean structural benefit: an exclusive table lock also serves as the
physical write latch, so only one writer touches a table's pages at a time and the
write path needs no separate page latches. The lock manager itself is written
over arbitrary resource ids and supports finer (row) granularity.

A clear separation is maintained between **latches and locks**: physical
structures (the buffer pool, the catalog cache, the WAL) are protected by
short-lived latches held only for a single operation, while logical isolation is
provided by the long-lived two-phase locks held to end-of-transaction.

Deadlock is resolved by **wait-for-graph cycle detection** rather than a timeout.
When a transaction would block, edges to the transactions it waits on are added
and the graph is searched for a cycle; the transaction that closes a cycle is the
victim and is rolled back. Cycle detection is prioritized over a timeout because
it is precise (no false aborts from slow-but-progressing transactions) and
deterministic.

## Recovery

Logging is **logical and keyed by primary key** (record images per
insert/delete) rather than physical page-image logging. Logical records keyed by
PK make both redo (an idempotent upsert) and undo (its inverse) idempotent, which
prioritizes **simplicity and engine-independence** over the precision of
page-level logging; it avoids threading a log sequence number through every page
type.

The write-ahead rule is enforced **coarsely**: the buffer pool fsyncs the entire
WAL before writing any dirty page. This prioritizes a **simple, obviously correct
invariant** ("the log is durable before any data page it describes") over the
finer-grained efficiency of per-page LSN comparison.

Commit uses a **no-force policy** — a commit is made durable by fsyncing the log,
not by forcing the transaction's data pages to disk — so recovery must redo
committed work whose pages were still buffered at a crash. Recovery is
**Analysis → Redo → Undo** (redo committed operations forward, undo the rest in
reverse). **No fuzzy checkpoint** is taken: the log is replayed in full on open
and truncated only at a clean shutdown. Full replay is prioritized over
checkpoint machinery because it is simpler and correct at project log sizes.

## Extension Track C — LSM-tree storage

The LSM engine implements the same `StorageEngine` interface as the classic
engine. Writes are **sequential**: append to a WAL, then insert into a sorted
in-memory MemTable; when the MemTable fills, it is flushed in key order to an
immutable SSTable. This write path prioritizes **write throughput** (sequential
appends, no in-place random page writes) over read-path simplicity.

The MemTable is a **balanced tree (`std::map`)** rather than a skip list. A
balanced tree is chosen for correctness and simplicity; a skip list is the more
typical LSM structure and is the natural future substitution.

Each SSTable carries a **sparse index** (one entry every Nth key) and a **bloom
filter**. The sparse index bounds a point lookup to a short scan instead of a full
table read; the bloom filter lets a lookup skip a table whose keys it does not
contain. Together they prioritize **bounded read amplification** as SSTables
accumulate.

Compaction is **size-tiered, implemented as a full k-way merge** (newest value per
key wins, tombstones dropped). Size-tiered compaction is prioritized over leveled
compaction because it is materially simpler to implement correctly; leveled
compaction is the standard alternative with a different read/space trade-off.

---

## Trade-off register

| Decision | Prioritizes | Over | Why sound |
|---|---|---|---|
| KV `StorageEngine` seam, rows mapped to KV | clean engine swap, single relational layer | engine-specific tuning | enables a true A/B benchmark |
| `Status` for expected errors, exceptions for corruption | explicit control flow | uniform exception use | fewer silent failures on the hot path |
| Slotted-page tombstone delete | stable RIDs | in-page space reclaim | reclaim needs page rewrite; not required at scale |
| Single global page space | recovery simplicity | per-table isolation | one address space to redo |
| Fixed-fanout, bounded-key B+tree | provably correct splits | packing long keys | integer keys are 9 bytes |
| Lazy B+tree delete (no merge) | correctness, bounded complexity | optimal node occupancy | reads do not depend on rebalancing |
| Sidecar catalog file, not WAL-logged | simplicity on a rare path | transactional DDL | recovery targets row-level DML |
| Block nested-loop join only | generality, clarity | large-scale join throughput | correct for any predicate |
| Table-level locking | low bug surface; X lock = write latch | maximum concurrency | correctness over throughput |
| Wait-for cycle detection | precision, determinism | timeout simplicity | no false aborts |
| Logical PK-keyed WAL | idempotent redo/undo, engine independence | page-level precision | avoids per-page LSN bookkeeping |
| Coarse write-ahead (fsync log before any page) | one simple invariant | per-page LSN efficiency | obviously correct |
| Full WAL replay, truncate on clean shutdown | simplicity | online checkpointing | correct at project log sizes |
| LSM `std::map` MemTable | correctness, simplicity | skip-list authenticity | natural future substitution |
| Size-tiered (full-merge) compaction | implementation simplicity | leveled read/space profile | correct and bounds amplification |
