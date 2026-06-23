# WAL & Crash-Recovery Demo Guide

## WAL Design

MiniDB uses a **Write-Ahead Log (WAL)** in `minidb.wal`.

Each `LogRecord` is 20 bytes (binary, fixed-size):

| Field | Type | Size | Description |
|---|---|---|---|
| lsn | int32 | 4 | Monotonically increasing sequence number |
| txn_id | int32 | 4 | Transaction ID |
| type | uint8 | 1 | BEGIN / INSERT / DELETE / COMMIT / ABORT |
| _pad | uint8[3] | 3 | Alignment padding |
| record_id | int32 | 4 | Affected row ID |
| record_val | int32 | 4 | Row value (INSERT only) |

## WAL Write Rules

1. Log the operation **before** applying it to the heap.
2. `COMMIT` record is flushed to disk before telling the user "committed".
3. Only committed transactions are redone during recovery.

## Crash-Recovery Demo

```bash
cd build/
./minidb
```

```
minidb> INSERT 10 1000
minidb> INSERT 20 2000
minidb> SHOW           ← confirm 2 rows
minidb> CRASH          ← deletes minidb.db, keeps minidb.wal
minidb> RECOVER        ← replays WAL, rebuilds state
minidb> SHOW           ← should see the same 2 rows again
minidb> QUIT
```

Or run the script:

```bash
bash scripts/demo_crash_recovery.sh
```

## Recovery Algorithm (Redo-Only)

1. **Pass 1**: Read entire WAL. Collect all `COMMIT` txn IDs and their log records.
2. **Pass 2**: Sort records by LSN. Redo `INSERT` / `DELETE` for committed TXNs only.
3. Duplicate-key check prevents double-insert (idempotent redo).
