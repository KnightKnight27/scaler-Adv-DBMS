# Replication Demo Guide (Track D)

## Design

MiniDB implements **primary-replica log-shipping**.

- The **Primary** node handles all writes. Every INSERT/DELETE is written to the WAL before being applied.
- The **Replica** node runs a background thread (`Replica::WorkerLoop`) that tails the WAL file every 300 ms and applies committed transactions to a separate storage file (`minidb_replica.db`).

## Running the Demo

```bash
# From build/
./minidb
```

Inside the REPL:
```
minidb> INSERT 1 100
minidb> INSERT 2 200
minidb> INSERT 3 300
minidb> REPLICA      ← check replica after ~300ms sync delay
minidb> DELETE 2
minidb> REPLICA      ← replica should reflect the deletion
```

## Failure Simulation

```
minidb> DEMO         ← runs the full automated demo including a blocking demo
```

The 2PL demo shows TXN-2 blocking on a row held by TXN-1, then unblocking after commit.

## What Happens Internally

1. `Primary::Execute` is called.
2. `TxManager::Begin` allocates a txn_id and logs `BEGIN`.
3. `LockManager::LockExclusive` blocks until the row is free.
4. WAL appends `INSERT` / `DELETE` record.
5. `Executor::Execute` applies the change to `HeapFile` + `BPlusTree`.
6. `TxManager::Commit` logs `COMMIT`, flushes WAL, releases locks.
7. `Replica::WorkerLoop` wakes, reads new WAL records, applies committed ones to replica storage.
