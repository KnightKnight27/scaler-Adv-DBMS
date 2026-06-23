# Lab 8 - In-Memory Transaction Manager

Name: Aparna Singha  
Roll Number: 24BCS10353

## Objective

The objective of this lab is to simulate important DBMS transaction concepts in C++17 without using any real database engine. The program manages a small in-memory bank ledger and demonstrates transaction begin, read, write, commit, abort, MVCC visibility, strict locking, deadlock detection, and cleanup of old versions.

## Folder Structure

```text
Lab8/
├── README.md
└── transaction-manager/
    ├── main.cpp
    ├── Makefile
    └── run_tests.sh
```

## What This Lab Implements

This lab implements a small transaction manager for account balances stored only in memory. The code shows:

- transaction start with snapshot timestamp
- MVCC version chains for each account
- strict two-phase locking for writes
- commit and abort handling
- rollback by clearing staged writes
- waits-for graph construction
- deadlock detection with victim selection
- lost update prevention under snapshot isolation
- vacuum cleanup for old versions
- normal demo mode and `--test` mode

## Dataset Used

The dataset is a simple bank account ledger:

- `ACC1001`
- `ACC1002`
- `ACC1003`
- `ACC1004`

Each account stores an integer balance. Everything is stored inside program memory only. No real database engine is used, no file persistence is implemented, and the whole project is only a lab simulation of transaction management concepts.

## Transaction Lifecycle

Each transaction goes through a small lifecycle:

1. `beginTransaction()` creates a new transaction id and snapshot timestamp.
2. `read()` checks staged writes first and otherwise reads the latest visible committed version.
3. `write()` and `erase()` stage changes until commit.
4. `commit()` turns staged writes into committed versions and releases locks.
5. `abort()` discards staged writes and releases locks.

Because changes stay inside the write set until commit, rollback is straightforward.

## MVCC Explanation

Every account keeps a version chain instead of only one current balance. A committed write appends a new version with a commit timestamp. Older versions are marked as expired when they are replaced.

A transaction reads the version visible to its snapshot. This means:

- it can still see older committed data if a newer transaction commits later,
- it can see its own uncommitted writes from the local write set,
- later commits from other transactions do not suddenly change the active transaction's view.

This is the main idea shown in the snapshot isolation demo.

## Strict 2PL Explanation

The program uses strict two-phase locking for write operations:

- a writer must acquire an exclusive lock before changing an account,
- another writer must wait if the lock is already held,
- the lock is released only when the transaction commits or aborts.

This keeps concurrent writes safe and also makes deadlock scenarios easy to demonstrate.

## Waits-For Graph and Deadlock Detection

When one transaction waits for another transaction's lock, the manager adds an edge in a waits-for graph. After adding the edge, it runs cycle detection using DFS.

If a cycle is found, the youngest transaction is chosen as the victim. In this program, "youngest" means the transaction with the highest transaction id. The victim is aborted, its locks are released, and waiting transactions are notified so execution can continue.

## Commit and Abort Handling

On commit:

- the manager checks that the transaction is still active,
- staged writes are converted into committed account versions,
- older open versions are marked expired,
- a commit timestamp is assigned,
- locks are released.

On abort:

- staged writes are removed,
- no committed data is changed,
- locks are released,
- waits-for graph edges are cleared.

## Lost Update Prevention

The manager also checks for snapshot conflicts. If a transaction tries to write an account after another committed transaction has already changed that account beyond the writer's snapshot, the write is rejected with a serialization failure message.

This prevents lost updates under snapshot isolation and makes the behavior closer to real transaction systems.

## Vacuum / Garbage Collection

The `vacuum()` function removes expired versions that are no longer needed by any active snapshot. The implementation also keeps the latest account state and avoids removing versions that an active transaction may still need to read.

This part is only an in-memory cleanup routine, but it demonstrates the basic idea of garbage collection in MVCC systems.

## Demo Scenarios

The program runs these demo sections in normal mode:

1. Basic commit
2. Snapshot isolation
3. Abort and rollback
4. Strict 2PL write blocking
5. Deadlock detection
6. Lost update prevention
7. Vacuum cleanup

Each section prints readable logs such as transaction start, reads, writes, waiting, deadlock resolution, commit, abort, and version cleanup.

## Build and Run Instructions

Move into the project folder:

```bash
cd Lab8/transaction-manager
```

Build and run using the Makefile:

```bash
make build
make run
make test
```

Manual compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -pthread -O2 main.cpp -o tx_manager
./tx_manager
./tx_manager --test
```

## Test Mode

Test mode is available with:

```bash
./tx_manager --test
```

In test mode the program suppresses the long demo logs and runs internal checks for:

- basic commit
- snapshot isolation
- abort rollback
- write locking
- deadlock detection
- lost update prevention
- vacuum cleanup

The program returns exit code `0` if all tests pass and `1` if any test fails.

## Limitations

This program is a lab-scale simulation, so it has some limits:

- all data is in memory only
- no disk storage or recovery log is implemented
- there is no SQL layer or query parser in this lab
- only account balance records are modeled
- locking is focused on exclusive write locks
- the vacuum logic is simplified compared to production DBMS engines

## Conclusion

This lab helped demonstrate how transaction management ideas can be modeled directly in C++17. Even though the project does not use a real DBMS, it still shows the interaction between snapshots, locks, deadlocks, rollback, version chains, and cleanup in a clear and practical way.
