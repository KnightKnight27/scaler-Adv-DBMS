# Recovery

M5 implements write-ahead logging and restart recovery for transaction changes.

## WAL Records

The log manager writes append-only records for:

- `BEGIN`
- `INSERT`
- `DELETE`
- `COMMIT`
- `ABORT`

Each log record has an LSN, transaction id, table name, RID, and row payload
where applicable. Table names and row payloads are hex-encoded in the log so
encoded rows can safely contain separators.

## Write-Ahead Behavior

`WalLogManager` appends and flushes records as they are written. A transaction is
treated as durable only after its `COMMIT` record is present in the WAL.

## Restart Recovery

Recovery reads the full WAL and performs redo for committed transactions:

- Committed inserts are restored.
- Committed deletes remove matching rows.
- Uncommitted or aborted transaction changes are ignored.

This gives the capstone demo a concrete crash-recovery flow: write committed and
uncommitted records, simulate a crash by reopening the WAL, then recover only
committed effects.

## Current Limitations

- Recovery currently rebuilds a logical recovered-row view from the WAL. It does
  not yet rewrite heap pages in place.
- Checkpointing is not implemented; recovery scans the full log.
