# Final Demo Script

Use this script for the capstone walkthrough after M5.

## M1 Storage

- Create a heap table.
- Insert records until multiple heap pages are allocated.
- Show scan results before and after delete.
- Explain buffer pool pin counts, dirty pages, and eviction.

## M2 Indexing And Parser

- Insert primary keys into the B+ tree.
- Search for an existing key and a missing key.
- Delete a key and show that it no longer appears.
- Parse `INSERT`, `SELECT`, and `DELETE` statements and show the structured output.

## M3 Query Execution

- Create `users` and `orders`.
- Run `INSERT`.
- Run `SELECT ... WHERE id = ...` and explain primary-key index lookup.
- Run an equality join.
- Run `COUNT(*)`.

## M4 Transactions

- Start two reader transactions and acquire shared locks on the same resource.
- Start one writer transaction and show it waits for an exclusive lock.
- Commit readers and retry the writer lock.
- Demonstrate deadlock:
  - T1 locks A.
  - T2 locks B.
  - T1 waits for B.
  - T2 requests A.
  - The wait-for graph detects a cycle and aborts T2.

## M5 Recovery

- Log a committed insert.
- Log an uncommitted insert.
- Simulate a crash by reopening the WAL.
- Run recovery and show only the committed row is recovered.
- Log a committed delete and show replay removes the row.

## Viva Talking Points

- Storage favors simplicity: fixed-size pages with slotted records.
- Index is a primary-key B+ tree with sorted leaf scans.
- Query execution uses heap scans except primary-key equality lookups.
- Transactions use strict 2PL for serializable behavior.
- Recovery replays committed WAL records and ignores uncommitted changes.
- Track B MVCC remains the extension step after the required 2PL baseline.
