# Transactions

M4 implements the required core transaction path using strict two-phase locking.

## Transaction Lifecycle

- `TransactionManager::Begin()` creates an active transaction in the growing phase.
- Transactions acquire locks through the transaction manager.
- `Commit()` releases all locks and marks the transaction committed.
- `Abort()` releases all locks and marks the transaction aborted.

Locks are held until commit or abort. This is strict 2PL, so readers and writers
do not release locks early.

## Locking

The lock manager supports:

- Shared locks for reads.
- Exclusive locks for writes.
- Shared/shared compatibility.
- Exclusive conflicts against other shared or exclusive holders.
- Lock upgrade from shared to exclusive when no other transaction holds the
  resource.

Resources are represented as stable strings such as `users:1` or `page:A`. Later
execution milestones can map table rows and pages onto those resource names.

## Deadlock Handling

When a lock cannot be granted, the lock manager records a wait-for edge from the
requesting transaction to the blocking transactions. If that creates a cycle, the
transaction manager aborts the requester and releases its locks.

The test suite demonstrates the classic two-transaction deadlock:

- T1 locks A.
- T2 locks B.
- T1 waits for B.
- T2 requests A, forming a cycle.
- T2 is aborted and T1 can continue.

## Current Limitations

- Waiting is represented by a `false` return value; the caller retries after
  blockers commit or abort.
- The execution engine is not yet transaction-aware. M4 supplies the transaction
  subsystem required before wiring statements through transactions.
