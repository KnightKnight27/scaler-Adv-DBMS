# MiniDB Architecture

## Overview

```text
+-----------------------+
| SQL Parser            |
+-----------+-----------+
            |
            v
+-----------------------+
| Optimizer / EXPLAIN   |
+-----------+-----------+
            |
            v
+-----------------------+
| Executor              |
| - table scan          |
| - index scan          |
| - nested-loop join    |
| - insert / delete     |
+-----------+-----------+
            |
            v
+-----------------------+      +----------------------+
| Transaction Manager   |<---->| Lock Manager         |
| - BEGIN/COMMIT/ROLL   |      | - S/X locks          |
| - strict 2PL hooks    |      | - waits-for graph    |
| - MVCC/WAL hooks      |      | - deadlock detect    |
+-----------+-----------+      +----------------------+
            |
            v
+-----------------------+
| Storage Engine        |
| - heap files          |
| - buffer pool         |
| - B+ Tree indexes     |
+-----------+-----------+
            |
            v
+-----------------------+
| Page Manager / Files  |
+-----------------------+
```

## Storage Layer

- Each table uses a persisted heap file under `data/`.
- Heap pages are fixed-size 4096-byte slotted pages.
- Rows are stored as stable JSON payloads keyed by column name.
- `RecordID(page_id, slot_id)` addresses rows directly inside heap files.

## Page Format

- Page header:
  `num_slots` and `free_end`.
- Slot directory:
  one fixed 8-byte slot entry per record.
- Record area:
  variable-length payloads packed from the end of the page backward.
- Deletes are tombstones so rollback can restore rows in place.

## Buffer Pool

- `BufferPoolManager` exposes `fetch_page`, `new_page`, `unpin_page`, `flush_page`, and `flush_all_pages`.
- LRU ordering drives eviction.
- Pinned pages are never evicted.
- Debug logs capture hit, miss, eviction, and flush events.

## B+ Tree Design

- Integer-key B+ Tree with leaf splits and internal node splits.
- Primary key indexes are created automatically for each table.
- Additional integer indexes can be created with `CREATE INDEX`.
- Index entries map keys to one or more `RecordID` values.

## Parser

- The parser supports:
  `CREATE TABLE`, `CREATE INDEX`, `INSERT`, `SELECT`, `DELETE`, `JOIN`, `EXPLAIN`, `BEGIN`, `COMMIT`, `ROLLBACK`, and `SET MODE`.
- The supported predicate form is equality (`column = value`).
- `JOIN` is implemented for `SELECT *` and `COUNT(*)` with an equi-join predicate.

## Executor

- `INSERT` writes into heap files and updates all relevant B+ Tree indexes.
- `SELECT` uses either table scan or index scan depending on the optimizer plan.
- `DELETE` removes rows from heap storage and index entries.
- `JOIN` uses nested-loop execution with optimizer-selected outer/inner order.
- `COUNT(*)` returns a scalar count for simple scans and joins.

## Optimizer

- Statistics tracked per table:
  row count and page count.
- Selectivity rules:
  primary-key equality uses `1 / row_count`.
- Fallback selectivity:
  unknown predicates use `0.1`.
- Plan choices:
  indexed equality predicates pick `INDEX_SCAN`; otherwise `TABLE_SCAN`.
- Join order:
  the smaller estimated input becomes the outer relation.

## Strict 2PL

- Reads request shared (`S`) locks.
- Writes and deletes request exclusive (`X`) locks.
- Locks are held until commit or rollback.
- Undo actions allow baseline statement rollback without full recovery support.

## Lock Compatibility Matrix

| Held \ Requested | Shared | Exclusive |
| --- | --- | --- |
| Shared | Allowed | Conflict |
| Exclusive | Conflict | Conflict |

## Deadlock Detection

- A waits-for graph is maintained for blocked lock requests.
- Cycle detection runs during lock waits.
- The youngest transaction, modeled as the highest transaction ID in the cycle, becomes the victim.
- Victim rollback releases locks so the remaining transaction can continue.

## Benchmark Baseline

- `benchmarks/run_baseline.py` loads sample data and measures insert, point lookup, table scan, join, and a concurrent 2PL workload.
- Results are written to:
  `benchmarks/benchmark_data.csv` and `benchmarks/benchmark_results.md`.

## How To Run

- Build:
  `python -m compileall src`
- Tests:
  `python -m unittest discover -s tests`
- Benchmarks:
  `python benchmarks/run_baseline.py`

## Demo Commands

- Core demo:
  `bash scripts/demo_core.sh`
- Deadlock demo:
  `bash scripts/demo_2pl_deadlock.sh`

## Limitations

- Recovery, WAL replay, and MVCC visibility are extension hooks only in this baseline.
- The SQL grammar is intentionally compact and targets the capstone-required statements.
- B+ Tree indexes currently support `INT` columns only.

## Contribution Notes For Person 1

- Extension points are already present for `WALManager`, `RecoveryManager`, `MVCCManager`, and `VersionStore`.
- The executor routes all read/write operations through transaction hooks.
- The storage and transaction layers are isolated so MVCC or recovery can replace behavior without rewriting the parser, optimizer, or executor.

