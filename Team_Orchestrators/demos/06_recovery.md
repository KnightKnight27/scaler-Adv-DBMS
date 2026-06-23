# Demo 6 — Crash recovery

Recovery is a process across two runs of the engine, so it is shown here rather
than as a single SQL script. The automated version is `tests/test_recovery.cpp`,
which is part of `ctest`.

## What happens

MiniDB uses a write-ahead log (`<base>.wal`) with a NO-FORCE + STEAL policy:

- Every insert/delete appends a WAL record (with a tuple image) and the log is
  flushed before the change can reach the data file.
- `COMMIT` appends a commit record and flushes the WAL — that is the durable
  commit point. Heap pages are **not** force-written at commit.
- On startup, `TransactionManager::recover` performs ARIES-lite recovery:
  1. **Analysis** — scan the WAL, find which transactions committed.
  2. **Redo** — replay committed inserts/deletes (idempotent by RID),
     reconstructing any change that had not yet reached the data file.
  3. **Undo** — reverse-apply records of transactions that never committed.
  Then it writes a checkpoint and truncates the log.

## Reproducing it

`tests/test_recovery.cpp` does the following:

1. Commit a transaction inserting rows 1, 2, 3.
2. Begin a second transaction inserting rows 4, 5 — **without committing** —
   and force its dirty pages to disk (simulating STEAL), then abandon the
   process without a clean shutdown (`debug_crash`).
3. Reopen the database. Recovery redoes the committed rows and undoes the
   in-flight rows.

Result: the reopened database contains exactly rows 1, 2, 3. The uncommitted
rows 4 and 5 are gone.

Run it with:

```bash
./build.sh test          # runs all suites, including test_recovery
ctest --test-dir build -R test_recovery --output-on-failure
```
