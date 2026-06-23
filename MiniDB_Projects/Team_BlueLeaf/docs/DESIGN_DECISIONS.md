# Design Decisions and Trade-offs

A database is a pile of engineering trade-offs. These are the ones we made and why.

## Storage

- Single file, 4 KiB pages, page is the unit of I/O. This matches the SQLite model from the lectures.
  The page is the unit because you cannot read a few random bytes from disk; you pay for a whole block.
- Slotted pages for variable-length rows. A slot directory of (offset, length) lets records be
  variable length and lets the page be compacted without changing record identity: a RID is
  (page_id, slot), and the slot index stays stable even when bytes move. That is exactly why an index
  can store a fixed RID per row.
- CRC32 per page. A cheap integrity check at the I/O boundary, so a torn page is detected, not
  silently served.

## Buffer pool: clock-sweep, STEAL plus NO-FORCE

- Clock-sweep over LRU (as taught): O(1) with a circular array plus a per-frame usage count, no
  ordered list to lock on every access, and the count cap makes it scan-resistant.
- STEAL plus NO-FORCE. Dirty pages can be written back on eviction (steal) and commit does not force
  data pages (no-force). This is the realistic, high-throughput choice, and it is what makes crash
  recovery necessary (a committed change may not be on disk yet, and an uncommitted one might be).

## Indexing: page-backed B+Tree

- Data only in leaves, leaves linked. Internal nodes hold only keys, which means higher fanout and a
  shallower tree and fewer disk seeks; linked leaves make range scans a sequential walk.
- Load-into-vectors then store-back nodes. Structural edits (split and merge) operate on std::vectors
  and rewrite the page. Nodes are at most 4 KiB, so this is cheap and far less error-prone than
  in-place byte shuffling.
- Delete without merge (documented simplification). erase removes the key from its leaf but does not
  rebalance underfull nodes. Search, insert, and range stay correct (internal keys are routing
  boundaries); only space efficiency suffers.

## Query execution and optimization

- Volcano (pull) iterators. They compose cleanly and use bounded memory for streaming operators.
- Two join algorithms. Nested-loop (any predicate) and hash join (equi-joins, O(n+m)); the optimizer
  builds the hash on the smaller table.
- Cost-based scan choice. A primary-key equality always uses the index; a range uses the index only
  if the estimated selectivity is at most 30 percent of the table, otherwise a sequential scan.
  Estimates come from a quick min/max/row-count stats scan (no persisted ANALYZE, a documented
  simplification).

## Transactions: Strict 2PL

- Shared and exclusive row locks, held until commit or abort. Strict 2PL means no dirty reads and no
  cascading aborts (writes are X-locked, and reads need S, which conflicts).
- Waits-for-graph deadlock detection. Before a transaction blocks we add its waits-for edges and DFS
  for a cycle; if granting would close one, the requester is aborted (immediate, no timeouts).
- Scope: row-granularity string keys; no gap or predicate locks (so we do not claim range-phantom
  protection); the victim is the cycle-closing requester (simple and livelock-free).

## Recovery: logical WAL with redo (and the ARIES contrast)

We use a logical (command) WAL: each committed transaction's PUT/ERASE ops are written to the WAL and
the COMMIT is flushed before the statement returns (force-log-at-commit; data pages are NO-FORCE). On
restart, RecoveryManager redoes the ops of committed transactions and skips the "losers" (no COMMIT),
which rolls them back. DDL is checkpointed immediately so structures are consistent, and replays are
idempotent (put-on-existing and erase-of-missing are no-ops).

Why this and not full ARIES? ARIES is the production answer and we understand it:

| | This design (logical redo) | ARIES (physiological) |
|---|---|---|
| Log granularity | logical row ops (PUT/ERASE) | per-page before/after images, by pageLSN |
| Passes | analysis plus redo (committed) | analysis, then redo (repeat history), then undo (losers) |
| Undo | not needed (we never apply losers) | CLRs (compensation log records), idempotent, restartable |
| Handles STEAL of uncommitted pages | only under NO-STEAL (we checkpoint DDL and assume the demo's working set is not stolen) | yes, via undo |
| Complexity | low, tractable, fully demonstrable | high (pageLSN, DPT/recLSN, ATT, CLR chains) |

We chose the logical-redo design for tractability and a clean, correct demo, while documenting how
ARIES would add an undo pass with CLRs and a Dirty Page Table to handle STEAL in the general case.

## Extension Track C: LSM vs B+Tree

- LSM is write-optimized. A write is an in-memory MemTable insert (sequential, O(1)); SSTables are
  flushed sequentially and merged by compaction. Reads check the MemTable then SSTables from newest to
  oldest, gated by Bloom filters.
- Trade-off measured (N=100k, working set in cache): the LSM does about 6.5x the row store's write
  throughput; the B+Tree is about 3.6x faster on point reads (the LSM pays read amplification across
  runs); space amplification is similar, with the LSM improving after compaction. As data exceeds RAM,
  the row store's random reads start hitting disk while the LSM stays Bloom-filter bounded.
- Simplifications: a full in-memory key index per SSTable (a real LSM uses a sparse block index plus a
  block cache), size-tiered full-merge compaction, and a std::map MemTable.
