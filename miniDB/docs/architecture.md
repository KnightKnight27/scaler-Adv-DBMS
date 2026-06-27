# Architecture

MiniDB is split into small modules: storage, buffer, index, catalog, SQL parsing, execution, optimizer, transaction, recovery, and CLI. M1 implements the storage and buffer foundation. Later milestones will fill the remaining module folders in the guideline order.

The system is intentionally compact for capstone review: each module has concrete behavior, focused tests, and clear trade-offs.
