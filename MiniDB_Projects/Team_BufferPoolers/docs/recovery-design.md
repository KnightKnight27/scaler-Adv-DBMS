# Recovery Subsystem Design

## Objective

The recovery subsystem ensures durability and consistency in the presence of crashes. MiniDB implements Write Ahead Logging (WAL) and crash recovery using log replay.

---

## Components

### WALManager

Responsible for:

* Generating log records
* Assigning Log Sequence Numbers (LSNs)
* Persisting records to disk before data pages are written

Example WAL record:

```
LSN=12
OPERATION=INSERT
DATA=1,Alice
```

---

### RecoveryManager

Responsible for:

* Reading WAL records after restart
* Replaying committed operations
* Restoring database state

---

## Recovery Procedure

1. Database starts.
2. WAL file is scanned.
3. Log records are loaded.
4. Operations are replayed in LSN order.
5. Storage state is restored.

---

## Crash Demonstration

1. Insert row.
2. WAL entry is written.
3. Simulate crash.
4. Restart database.
5. RecoveryManager replays logs.
6. Row becomes visible again.

---

## Complexity

| Operation       | Complexity |
| --------------- | ---------- |
| WAL Append      | O(1)       |
| Recovery Replay | O(N)       |

where N is the number of log records.

---

## Limitations

* No checkpointing
* No UNDO phase
* Redo-only recovery
* Single-node recovery
