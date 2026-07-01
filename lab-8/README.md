# Financial Ledger Transaction Processing Subsystem

**Author:** Sarthak Arora

**Domain:** Financial Banking & Asset Ledger Simulation

---

## Executive Summary

It demonstrates the hybrid concurrency control mechanisms utilized by leading relational databases (such as PostgreSQL) by combining **Multi-Version Concurrency Control (MVCC)** for non-blocking reads with **Strict Two-Phase Locking (2PL)** for mutually exclusive writes.

The engine operates in a fully deterministic, single-threaded simulation space. This enables robust, reproducible verification of complex transactional phenomena, including snapshot isolation preservation, write-lock contention, wait-for graph deadlock resolution, commit-time serialization conflicts, and historical garbage collection.

---

## Core Concurrency Architecture

- **MVCC Snapshot Isolation:** When a user session begins, the system freezes a virtual timestamp. All subsequent read queries fetch historical data states valid at that specific horizon, guaranteeing that long-running audits are never blocked by concurrent deposits or withdrawals.
- **Strict Two-Phase Locking (2PL):** Asset mutations require exclusive write locks. Once granted, these locks are held until the transaction reaches absolute finality (either via commit or rollback).
- **Wait-For Graph Deadlock Detection:** Contested locks generate directed edges in an internal wait-graph. The engine continuously runs cycle-detection traversals; upon detecting a circular deadlock, it autonomously aborts the youngest transaction (highest TxID) to break the cycle.
- **First-Updater-Wins Commit Validation:** Before persisting staged mutations, the engine verifies whether concurrent transactions have committed newer versions of those specific assets. If an unobserved overwrite is detected, the transaction fails with a commit conflict.
- **Garbage Collection (Vacuum Engine):** An automated cleanup routine sweeps the ledger to reclaim memory, purging obsolete historical asset states that are older than the oldest active transaction horizon.

---

## Source File Breakdown

- **transaction_engine.h:** Defines the core data representation (`AssetVersion`, `StagedMutation`, `LedgerSession`) and the central `LedgerCoordinator` class.
- **transaction_engine.cpp:** Implements the Multi-Version snapshot lookup, 2PL lock table management, DFS wait-graph cycle resolution, and vacuum pruning.
- **main.cpp:** The test harness executing five distinct financial scenarios (Snapshot Isolation, 2PL Contention, Deadlock Teardown, Overwrite Protection, and GC Pruning).
- **Makefile:** POSIX build automation script.

---

## Build & Execution Guide

Open your terminal in the project directory.

**To compile and run the entire verification suite automatically:**

    make run

**To compile manually:**

    c++ -std=c++17 -O3 -Wall -Wextra main.cpp transaction_engine.cpp -o asset_ledger_engine
    ./asset_ledger_engine

**To clean compiled build artifacts:**

    make clean

---

## Verification Scenarios (`main.cpp`)

1. **Snapshot Isolation Verification:** Demonstrates that a snapshot reader reading `acc_alice` ($1000) remains completely unaffected when a concurrent writer mutates and commits `acc_alice` to $2500.
2. **Strict 2PL Contention:** Proves that an active mutation on `asset_gold` blocks secondary transactions until the primary lock owner concludes its lifecycle.
3. **Deadlock Teardown:** Constructs a circular escrow claim between two traders, confirming that the engine intercepts the deadlock and tears down the younger transaction.
4. **First-Updater-Wins Enforcement:** Generates a blind concurrent overwrite on `stock_tesla`, confirming that the lagging commit attempt is safely rejected.
5. **Garbage Collection Pruning:** Executes the vacuum sweep to confirm that unreachable historical asset versions are destroyed while active snapshots remain pristine.