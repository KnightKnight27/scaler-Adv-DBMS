# M5 Recovery Workload

Use this workload to demonstrate WAL behavior in the viva.

## Steps

1. Begin transaction 1.
2. Log insert for `users` row `1|Ada`.
3. Commit transaction 1.
4. Begin transaction 2.
5. Log insert for `users` row `2|Uncommitted`.
6. Simulate crash before transaction 2 commits by destroying the log manager.
7. Reopen the WAL and run recovery.

## Expected Result

- Transaction 1 appears in `committed_txns`.
- Transaction 2 appears in `ignored_txns`.
- Recovered `users` contains only `1|Ada`.

## Delete Replay

1. Begin transaction 1.
2. Log insert for `users` row `1|Ada`.
3. Commit transaction 1.
4. Begin transaction 2.
5. Log delete for the same RID.
6. Commit transaction 2.
7. Reopen the WAL and run recovery.

Expected recovered `users` table is empty because the committed delete is replayed.
