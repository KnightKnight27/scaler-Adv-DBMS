# MiniDB Design Notes

The guiding rule for the whole project: build the real thing, but the simplest
honest version of it, and write down every shortcut so it is a deliberate
decision rather than a gap.

## Layered architecture

```
            SQL text
               |
   parser    lexer + recursive-descent parser -> AST
               |
   optimizer  AST -> plan (index scan vs seq scan, join order)
               |
   executor   Volcano iterator operators pull rows up the tree
               |
   catalog    per-table schema + heap file + B+ tree index
     /     \
  btree    storage   (heap file -> buffer pool -> disk manager -> 4 KB pages)

   txn        2PL lock manager + deadlock detection
   recovery   write-ahead log + redo/undo
   mvcc       version chains + snapshot isolation (extension)
```

`engine` is the glue: it parses, optimizes, runs operators for SELECT, and
applies INSERT/DELETE to the heap and index. `main` is the SQL shell.

## Data formats

Row encoding is self-describing so a page can be decoded on its own:

| Type | Bytes |
|------|-------|
| INT  | `0x01` + 8 bytes, big-endian signed |
| TEXT | `0x02` + 2-byte length + UTF-8 bytes |

Slotted page (4 KB): a 4-byte header (`num_slots`, `free_ptr`), a slot directory
growing from the front, and tuple bytes packed from the back. The gap between
them is the free space.

## Design decisions and trade-offs

1. **One heap file per table, fixed 4 KB pages.** Simple to reason about and to
   demonstrate.
2. **Buffer pool: LRU, dirty write-back, no pin counts.** The read/write path is
   single-threaded, so pin counts and page latches are unnecessary.
3. **B+ tree index in memory, rebuilt on startup** by scanning the heap, so it
   is never persisted or stale. Startup cost is O(rows) in exchange for simple,
   always-correct index code.
4. **Lazy delete in the B+ tree:** a deleted entry is removed from its leaf but
   underfull nodes are not merged. Routing keys still lead lookups to the right
   leaf, so search and range scans stay correct.
5. **First column = primary key,** unique and (to be indexed) INT. Detail tables
   carry their own surrogate key. Duplicate primary keys are rejected.
6. **No page compaction on delete:** a deleted slot is tombstoned and its bytes
   are not reclaimed.
7. **Volcano (iterator) execution:** every operator is a pull-based iterator, so
   plans compose and only the rows that are needed are produced.
8. **Cost-based optimizer, two heuristics:** use an index scan for an equality
   on the indexed primary key (matches at most one row); for a two-table join
   make the smaller table the inner, buffered relation.
9. **Concurrency is modelled as interleaved lock requests** from a single driver
   (a textbook 2PL schedule), not OS threads, so the deadlock demo and the
   MVCC-vs-2PL benchmark are deterministic and easy to explain. Concurrency
   control is purely logical; physical page access stays single-threaded.
10. **Recovery is demonstrated on a key -> value store** rebuilt from the WAL.
    This shows the write-ahead and redo/undo guarantees clearly without
    entangling the slotted-page format.

## Transactions (txn)

Strict 2PL with shared/exclusive locks per key. A request that conflicts with a
current holder adds an edge to a waits-for graph; a cycle in that graph is a
deadlock, and the youngest transaction is aborted to break it. Strict 2PL holds
all locks until commit/abort, which gives serializable isolation.

## Recovery (recovery)

The WAL appends one record per event (BEGIN / UPDATE with before- and
after-images / COMMIT) and flushes before the data is touched. On restart,
analysis finds the committed transactions (winners), redo re-applies their
updates, and undo rolls back the rest using the before-images.

## Extension - Track B: MVCC (mvcc)

Each key keeps a chain of versions tagged with the creating transaction id. A
reader uses a snapshot (its start id) and sees the newest committed version with
id <= snapshot; a writer appends a new version. Readers therefore never block
writers and vice versa, which the benchmark confirms against the 2PL baseline.
