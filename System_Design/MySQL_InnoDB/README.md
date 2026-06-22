# MySQL / InnoDB Storage Engine

- **Role Number:** 24BCS10345
- **Name:** Ansh Mahajan

InnoDB is MySQL's default transactional storage engine. Its defining choice is
that **tables are stored *inside* their primary-key index** — the table *is* a
B+tree keyed on the primary key. This is called a *clustered index* or an
*index-organized table*, and almost everything else about InnoDB follows from
it.

## 1. On-disk layout

InnoDB stores data in **pages of 16 KB** by default (compare SQLite's 4 KB from
Lab 4 and PostgreSQL's 8 KB). Pages are grouped into *extents* (1 MB = 64
pages) and *segments*, which live inside per-table `.ibd` files (with
`innodb_file_per_table` on) plus shared system tablespaces.

```
tablespace (.ibd)
  └─ segment(s)
       └─ extent (1 MB = 64 pages)
            └─ page (16 KB)
                 ├─ page header (page no, type, LSN, ...)
                 ├─ records (sorted by key, singly linked)
                 ├─ page directory (sparse slots for binary search)
                 └─ trailer (checksum, LSN echo)
```

## 2. The clustered index (and why secondary indexes are different)

```
            [ key:30 | key:70 ]            <- internal (directory) pages
           /         |         \
   [10|20|30]   [40|50|60]   [70|80|90]    <- leaf pages hold FULL rows
```

- **Leaf pages of the clustered index hold the entire row**, in primary-key
  order. A primary-key lookup therefore lands directly on the row — no extra
  indirection.
- A **secondary index** is a separate B+tree whose leaves store the indexed
  column(s) **plus the primary key** (not a physical pointer). So a query that
  filters on a secondary index does two descents: one in the secondary tree to
  find the PK, then one in the clustered tree to fetch the row. This is the
  famous InnoDB *"secondary index returns the PK, then we look up the row"*
  double-lookup, and the reason a fat primary key bloats every secondary index.
- If you declare no primary key, InnoDB invents a hidden 6-byte `DB_ROW_ID`.

This is the production cousin of the B-tree built in **Lab 6** — same balanced
multi-way structure, but B+tree (values only in leaves, leaves linked for range
scans) and update-in-place rather than copy.

## 3. The buffer pool

The buffer pool caches pages in memory and is the single most important
performance knob (`innodb_buffer_pool_size`). It uses a **midpoint-insertion
LRU**: newly read pages are inserted at the *midpoint* (the 3/8 boundary by
default), not the head, so a one-off full scan cannot evict the genuinely hot
working set. Pages age from the "young" sublist to the "old" sublist.

This is the same family of ideas as the **Lab 3** CLOCK-sweep cache; InnoDB's
LRU variant is just tuned to resist scan pollution.

## 4. Durability: redo log, undo log, doublewrite

InnoDB separates "make it durable" from "make it visible":

- **Redo log** (`ib_logfile*`, a physical/physiological log of page changes) —
  written sequentially and flushed on commit. This gives **WAL**: the log hits
  disk before the dirty data pages do, so a crash can be recovered by replaying
  redo. Dirty pages are flushed lazily in the background.
- **Undo log** (rollback segments) — stores the *previous* versions of rows.
  Used both to roll back an aborted transaction and to reconstruct old row
  versions for MVCC reads.
- **Doublewrite buffer** — because a 16 KB page write is not atomic on most
  hardware, InnoDB first writes the page to a sequential doublewrite area, then
  to its real location. A crash mid-write can always recover a clean copy,
  defeating *torn pages*.

```
COMMIT path:
  modify page in buffer pool ──► append change to redo log ──► fsync redo
                                                                   │ (commit returns)
  later, checkpoint: dirty pages ──► doublewrite ──► final page location
```

## 5. MVCC, the InnoDB way

Every row carries hidden columns `DB_TRX_ID` (last writer) and `DB_ROLL_PTR`
(pointer into the undo log). A transaction takes a **read view** (a snapshot of
which transaction ids are committed). When it reads a row whose `DB_TRX_ID` is
not visible, it walks the `DB_ROLL_PTR` undo chain backward to reconstruct the
version it *should* see.

Contrast this with **Lab 8**, where new versions were appended to a chain in
place: InnoDB keeps the *current* row in the index and pushes *old* versions
into the undo log. PostgreSQL (next document) does the opposite — old versions
stay in the heap. InnoDB's default isolation is **REPEATABLE READ**, and it uses
**next-key locking** (record + gap locks) to stop phantom rows.

## 6. Trade-offs

| Property | InnoDB's position |
| --- | --- |
| Read amplification | Low for PK reads (row is in the leaf); +1 tree for secondary-index reads. |
| Write amplification | Moderate–high: update-in-place + redo + doublewrite + page splits. |
| Space amplification | Low–moderate; undo and fragmentation from random inserts add some. |
| Range scans | Excellent — clustered leaves are key-ordered and linked. |
| Random insert (UUID PK) | Painful — scatters inserts, causes page splits; sequential PKs are far better. |

## 7. Observations / takeaways

- The clustered index makes **primary-key choice an architectural decision**:
  use a small, monotonically increasing key so inserts append to the right edge
  instead of splitting random pages, and to keep secondary indexes slim.
- InnoDB is **WAL-based**, like nearly every serious engine — the redo log is
  the durability backbone and the reason commits are fast despite lazy page
  flushing.
- Doublewrite is a reminder that "write a page" is not atomic at the hardware
  level; correctness requires designing around torn writes.

## References

- MySQL Reference Manual — *InnoDB Storage Engine* (Ch. 17), Oracle.
- J. Gray & A. Reuter, *Transaction Processing: Concepts and Techniques*.
- B. Schwartz et al., *High Performance MySQL*, O'Reilly.
