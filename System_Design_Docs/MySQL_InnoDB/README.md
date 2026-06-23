# MySQL InnoDB

## Student Details
- Name: Harshit Tiwari
- Roll Number: 24BCS10277

## Architecture
InnoDB is MySQL's default transactional storage engine. It provides ACID transactions, row-level locking, clustered indexes, crash recovery, and MVCC-style consistent reads.

## Clustered Index
InnoDB stores table rows inside the primary-key B+Tree. This is called a clustered index. Secondary indexes store the secondary key plus the primary key, then use the primary key to find the full row.

Implication:
- Primary key choice affects physical locality.
- Narrow, stable, increasing primary keys are usually friendly to insert-heavy workloads.
- Secondary index lookups can require an extra primary-key lookup.

## Locking
InnoDB supports row locks, gap locks, and next-key locks. These are important for repeatable reads and for preventing phantom rows in some cases.

## Redo And Undo
- Redo log: used to replay committed changes after a crash.
- Undo log: used for rollback and consistent reads.
- Buffer pool: caches data and index pages.
- Doublewrite buffer: protects against partial page writes.

## Design Tradeoffs
InnoDB favors transactional consistency and predictable recovery. The cost is extra metadata, redo/undo writes, and more complex locking behavior compared with simpler embedded engines.

## Key Takeaway
InnoDB is built around B+Trees, row-level transactions, and crash recovery. Its clustered-index design makes primary-key selection a major system-design decision.
