# Lab 8: Transaction Manager — MVCC + Strict 2PL + Deadlock Detection

## 🛠️ Compilation & Execution

```bash
g++ -std=c++17 txn_manager.cpp -o txn_manager
./txn_manager
```

---

## 📋 Overview

This lab implements a **complete transaction manager** combining three core concurrency-control mechanisms used in modern database systems:

### Component 1 — MVCC Version Chains

Each data item maintains a **linked list of versions** (newest → oldest). Every version stores:

| Field        | Description                                     |
|-------------|-------------------------------------------------|
| `value`     | The data value at this version                   |
| `created_by`| Transaction ID that wrote it                     |
| `begin_ts`  | Timestamp when version became visible            |
| `end_ts`    | Timestamp when superseded (`∞` if current)       |
| `prev`      | Pointer to the previous (older) version          |

**Snapshot Isolation**: Readers see a consistent point-in-time view. A transaction started at timestamp `T` reads the version where `begin_ts ≤ T < end_ts`.

### Component 2 — Strict Two-Phase Locking (S2PL)

| Lock Mode  | Shared (S) | Exclusive (X) |
|-----------|:----------:|:--------------:|
| **Shared (S)**    |    ✓       |      ✗         |
| **Exclusive (X)** |    ✗       |      ✗         |

- **Growing phase**: Locks acquired as needed (S for reads, X for writes)
- **No shrinking**: ALL locks held until `COMMIT` or `ABORT`
- Lock upgrades (S → X) supported when no other holders exist
- Conflicting requests enter a **wait queue**

### Component 3 — Deadlock Detection

- Builds a **Wait-For Graph**: edge `Ti → Tj` means "Ti is waiting for Tj to release a lock"
- **DFS cycle detection** identifies deadlocks
- **Resolution**: Abort the youngest transaction in the cycle (highest txn ID)

---

## 🖥️ Demo Scenarios

The program runs 4 automatic demos, then opens an interactive REPL:

| Scenario | What it demonstrates |
|----------|---------------------|
| 1 — MVCC Version Chains | Snapshot isolation: old txn reads stale data while new writes happen |
| 2 — Strict 2PL | S/X lock conflicts, blocking, shared-lock compatibility |
| 3 — Deadlock Detection | Two txns form a circular wait, detected via wait-for graph DFS |
| 4 — Combined Lifecycle | Bank transfer with full MVCC + S2PL + version chain history |

### Sample Output (Deadlock Scenario)

```text
── Step 1: Setup: T1 locks A, T2 locks B ──
  ✓ T1 WRITE 'A' = 10 (version @ts=17)
  ✓ T2 WRITE 'B' = 20 (version @ts=18)

── Step 2: T1 tries to write B → BLOCKED ──
  ⏳ T1 BLOCKED on X-lock for 'B'

── Step 3: T2 tries to write A → BLOCKED ──
  ⏳ T2 BLOCKED on X-lock for 'A'

── Step 4: Run deadlock detection ──
  Wait-For Graph Edges:
    T2 ──waits──▶ T1
    T1 ──waits──▶ T2

  🔴 DEADLOCK DETECTED! Cycle: T2 → T1 → T2
  Resolution: Aborting T2 (youngest in cycle)

── Step 5: T1 can now proceed ──
  ✓ T1 WRITE 'B' = 15
  ✓ T1 COMMITTED (all locks released)
```

---

## 🎮 Interactive Mode Commands

| Command | Description |
|---------|-------------|
| `begin` | Start a new transaction |
| `read <txn> <key>` | Read a key (S-lock + MVCC snapshot) |
| `write <txn> <key> <val>` | Write a value (X-lock + new version) |
| `commit <txn>` | Commit (release all locks) |
| `abort <txn>` | Abort (rollback versions + release locks) |
| `deadlock` | Run deadlock detection |
| `locks` | Display lock table |
| `versions` | Display MVCC version chains |
| `status` | Display transaction status table |
| `waitfor` | Display wait-for graph |
| `exit` | Quit |

---

## 🏗️ Architecture

```
┌──────────────────────────────────────────────────┐
│              TransactionManager                  │
│  ┌──────────────┐  ┌───────────┐  ┌───────────┐ │
│  │  MVCCStore    │  │ LockMgr   │  │ Deadlock  │ │
│  │              │  │  (S2PL)   │  │ Detector  │ │
│  │ key → chain  │  │ key →     │  │ wait-for  │ │
│  │  of versions │  │  lock     │  │  graph +  │ │
│  │  (linked     │  │  queue    │  │  DFS      │ │
│  │   list)      │  │  (S / X)  │  │  cycle    │ │
│  └──────────────┘  └───────────┘  └───────────┘ │
└──────────────────────────────────────────────────┘
```
