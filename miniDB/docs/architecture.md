# Architecture

MiniDB is split into small modules: storage, buffer, index, catalog, SQL parsing, execution, optimizer, transaction, recovery, and CLI. M1 implements the storage and buffer foundation. M2 adds the primary-key B+ tree index and structured SQL parser. M3 adds heap-backed statement execution, index lookups, nested-loop joins, and `COUNT(*)` aggregation. M4 adds strict 2PL transaction management with deadlock detection. Later milestones will fill recovery and MVCC behavior in the guideline order.

The system is intentionally compact for capstone review: each module has concrete behavior, focused tests, and clear trade-offs.
