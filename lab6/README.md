# Lab 6 — Transaction Manager with MVCC + Strict 2PL + Deadlock Detection

## Overview

This lab implements a complete transaction manager that combines three concurrency control mechanisms used in real database systems:

1. **MVCC (Multi-Version Concurrency Control)** — Readers see a consistent snapshot without blocking writers
2. **Strict 2PL (Two-Phase Locking)** — Prevents conflicts via shared/exclusive locks held until commit
3. **Deadlock Detection** — Wait-for graph with DFS cycle detection

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    Transaction Manager                        │
│                                                               │
│  begin() → read() → write() → commit() / abort()            │
│                                                               │
│  ┌─────────────────┐  ┌──────────────┐  ┌─────────────────┐ │
│  │   MVCC Manager   │  │ Lock Manager │  │    Deadlock     │ │
│  │                   │  │  (Strict 2PL)│  │    Detector     │ │
│  │ • Version chains │  │ • S/X locks  │  │ • Wait-for graph│ │
│  │ • Snapshots      │  │ • Lock table │  │ • DFS cycle     │ │
│  │ • Visibility     │  │ • Grant/Wait │  │ • Victim choice │ │
│  └─────────────────┘  └──────────────┘  └─────────────────┘ │
└──────────────────────────────────────────────────────────────┘
```

## MVCC Version Chains

Each row maintains a linked list of versions:

```
Row 1:
  [v0] created_by=T1, deleted_by=T3, value="Alice_v1"
    ↑
  [v1] created_by=T3, deleted_by=none, value="Alice_v2"  ← latest

Visibility Rule:
  A version is visible to Txn T if:
  1. created_by is committed AND started before T's snapshot
  2. NOT deleted by a committed transaction before T's snapshot
  3. OR created_by == T (own writes are always visible)
```

## Strict Two-Phase Locking

```
Transaction Lifecycle:
  ┌──────────┐     ┌──────────┐     ┌──────────┐
  │  BEGIN    │────▶│ GROWING  │────▶│ SHRINKING│
  │          │     │ (acquire │     │ (COMMIT/ │
  │          │     │  locks)  │     │  ABORT)  │
  └──────────┘     └──────────┘     └──────────┘

Lock Compatibility Matrix:
              Requested
              S     X
  Held  S  │ ✓  │  ✗  │    S = Shared (for reads)
        X  │ ✗  │  ✗  │    X = Exclusive (for writes)

Strict 2PL: All locks released ONLY at commit/abort (no early release)
This prevents cascading aborts and ensures serializability.
```

## Deadlock Detection

```
Wait-For Graph:
  T1 ──→ T2    (T1 waits for T2 to release a lock)
  T2 ──→ T3    (T2 waits for T3)
  T3 ──→ T1    (T3 waits for T1)  ← CYCLE = DEADLOCK!

Detection: DFS from each node, track "in-stack" nodes
  If we revisit an in-stack node → cycle found

Victim Selection: Abort the youngest transaction (highest TxnID)
  Rationale: youngest has done the least work
```

## Building and Running

```bash
make        # compile
make run    # compile and run
make clean  # cleanup
```

## Test Scenarios

| Test | What It Demonstrates |
|------|---------------------|
| **Test 1**: MVCC Versions | Version chains, snapshot isolation, visibility |
| **Test 2**: Strict 2PL | S/X lock compatibility, lock upgrade, release at commit |
| **Test 3**: Deadlock Detection | Wait-for graph cycles, victim selection |
| **Test 4**: Abort & Rollback | Undoing writes, restoring previous versions |
| **Test 5**: Banking Scenario | Complete transfer with concurrent readers |

## Files

| File | Description |
|------|-------------|
| `mvcc.h` | MVCC version chains, snapshots, visibility checks |
| `lock_manager.h` | S/X lock table, strict 2PL, lock upgrade |
| `deadlock_detector.h` | Wait-for graph, DFS cycle detection |
| `transaction_manager.h` | Unified API orchestrating all three components |
| `main.cpp` | Driver with 5 comprehensive test scenarios |
| `Makefile` | Build targets |

## How Components Integrate

1. **BEGIN**: MVCC creates a snapshot of active transactions
2. **READ**: Lock Manager acquires S lock → MVCC reads visible version
3. **WRITE**: Lock Manager acquires X lock → Deadlock Detector checks → MVCC creates new version
4. **COMMIT**: MVCC marks transaction committed → Lock Manager releases ALL locks (strict 2PL)
5. **ABORT**: MVCC undoes writes (removes versions) → Lock Manager releases locks
