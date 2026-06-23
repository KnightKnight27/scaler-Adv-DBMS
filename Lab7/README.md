# Database Transaction System: Lab 7 Implementation

This repository contains a single-file C++ application that illustrates the integration of **Multi-Version Concurrency Control (MVCC)** alongside **Strict Two-Phase Locking (2PL)** and **Cycle-Based Deadlock Detection**. 

## Project Structure

| File | Description |
|------|-------------|
| `txmgr.cpp` | Core logic and four testing scenarios. |
| `makefile` | Build instructions. |

## Compilation and Execution

You can compile and run the project using the provided makefile:

```bash
make          # Compiles the source into a binary
make run      # Compiles and executes the testing scenarios
make clean    # Deletes the compiled binary
```

Alternatively, you can build it directly via the compiler:

```bash
g++ -std=c++17 -pthread -o txmgr txmgr.cpp
./txmgr
```

## System Architecture

### 1. Multi-Version Concurrency Control (MVCC)

Instead of overwriting data in place, our system maintains a linked list of historical states (versions) for each record. Every new update or insertion prepends a fresh version to this list.

Each version records:
- The actual data payload.
- The ID of the transaction that created it.
- The ID of the transaction that deleted it (if applicable).

**Snapshot Visibility:** When a transaction starts, it receives a read snapshot. It can only read a version if:
- The version was created by a committed transaction prior to the current snapshot (or by the current transaction itself).
- The version hasn't been deleted, or the deleting transaction hasn't committed before the snapshot.

Because of MVCC, readers don't block writers—they can simply traverse the version history to find the appropriate state.

### 2. Strict 2PL Protocol

To guarantee serializability for write operations, transactions undergo two distinct stages:

- **Growing Stage:** The transaction is allowed to request locks but cannot release any.
- **Shrinking Stage:** Locks are dropped, and no new locks can be requested.

We employ *Strict* 2PL, meaning that all locks are retained until the transaction concludes (via commit or abort). This strategy prevents cascading rollbacks.
- **Shared (Read) Locks:** Multiple transactions can read a record concurrently.
- **Exclusive (Write) Locks:** Only one transaction can hold a write lock, blocking all others.

### 3. Cycle Detection for Deadlocks

Waiting for locks can lead to deadlocks. To resolve this, the system maintains a directed graph where edges represent transactions waiting for resources held by others.
Before putting a transaction to sleep, we perform a Depth-First Search (DFS) on this graph. If a cycle is detected, the requesting transaction is immediately aborted to resolve the deadlock.

## Testing Scenarios

The `txmgr.cpp` file includes a `main` function that executes the following tests:

1. **Snapshot Isolation:** A transaction reads historical data while another transaction modifies it.
2. **Shared Reading:** Multiple transactions successfully acquire read locks on the same record simultaneously.
3. **Write Blocking:** A reader is forced to wait until a concurrent writer commits its changes.
4. **Deadlock Resolution:** A circular lock dependency is intentionally created, triggering the cycle detector to abort one of the participants.

## Design Philosophy: Combining MVCC and 2PL

Relying purely on 2PL causes readers to block writers, reducing throughput. Pure MVCC can suffer from write-write conflicts if not carefully managed. By combining them:
- Reads remain lock-free (using snapshots) in principle, preventing readers from delaying writers.
- Writes use exclusive locks under Strict 2PL to maintain serialized, conflict-free updates.
- Potential deadlocks from exclusive locks are actively monitored and resolved via graph cycle detection.
