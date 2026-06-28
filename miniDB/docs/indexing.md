# Indexing

M2 implements an in-memory primary-key B+ tree used by later query execution and
optimizer milestones.

## B+ Tree Design

- Internal nodes store separator keys and child pointers.
- Leaf nodes store sorted primary keys and `Rid` values.
- Leaf nodes are linked left-to-right so ordered scans do not need to revisit
  internal nodes.
- Inserts update duplicate primary keys instead of creating duplicate entries.
- Leaf and internal nodes split when they exceed the configured key capacity.

## Operations

- `Search(key)` returns the row id for a primary key.
- `Insert(key, rid)` adds or updates a primary-key entry.
- `Delete(key)` removes a primary-key entry.
- `Scan()` returns all index entries in sorted key order.

## Parser Integration

The SQL parser produces structured `INSERT`, `SELECT`, and `DELETE` statements.
M2 tests connect parsed primary-key values and `WHERE id = ...` predicates to
the B+ tree. Full table execution remains part of M3.

## Current Limitations

- The index is in-memory; WAL and durable index rebuild/persistence are handled
  in later recovery work.
- Delete removes keys from leaves but does not rebalance underfull nodes.
