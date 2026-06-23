# Lab 8 — Transaction Manager: MVCC + Strict 2PL + Deadlock Detection

**Student:** Romit Raj Sahu | **Roll:** 24BCS10436

---

## 1. MVCC — Multi-Version Concurrency Control

### What is a "version"?

Every `write` creates a **new version** of a record rather than overwriting the old one. Old versions are retained so concurrent transactions can read them.

### Version chain

```
key "X" -> [
  Version{ value="original", created_by=T3, begin_ts=2, end_ts=4 },
  Version{ value="modified",  created_by=T5, begin_ts=4, end_ts=INF }
]
```

`end_ts=INF` means this is the **current** (live) version.

### Visibility rule

A transaction with `start_ts = T` can see a version only if:
```
version.begin_ts <= T  AND  version.end_ts > T
```

### Why reads don't block writes

Each reader sees a **consistent snapshot** frozen at its `start_ts`. A concurrent writer creates a new version with a new `begin_ts`; it does not invalidate the reader's snapshot. Readers and writers never conflict — no read locks are needed.

### What happens on abort

All versions created by the aborted transaction are removed. Any prior version whose `end_ts` was set to the aborted transaction's `begin_ts` (i.e., the version that was logically replaced) has its `end_ts` restored to `INF_TS`, making it current again.

---

## 2. Strict Two-Phase Locking (Strict 2PL)

### Two phases

- **Growing phase**: locks are acquired as operations are executed. A transaction can only acquire locks, not release them.
- **Shrinking phase**: ALL locks released at once, only at commit or abort.

### "Strict" means exclusive locks are held until the end

A **strict** 2PL variant holds exclusive (write) locks until transaction end. This prevents a second transaction from reading an uncommitted value (dirty read), because the X lock is not released until the first transaction commits or aborts.

### Lock compatibility table

| Held \ Requested | S (Shared) | X (Exclusive) |
|-----------------|-----------|--------------|
| **S** | ✓ Compatible | ✗ Blocked |
| **X** | ✗ Blocked | ✗ Blocked |

### Why releasing locks early is dangerous

Suppose T1 writes `X=100`, releases its X lock early, then T2 reads `X=100`. If T1 later aborts, T2 has read a value that never existed (dirty read). Strict 2PL prevents this by keeping X locks until T1 commits or aborts.

---

## 3. Deadlock Detection via Wait-For Graph

### What is a deadlock?

A deadlock occurs when two or more transactions form a cycle of mutual dependencies:
- T1 holds lock on `P`, waiting for `Q`
- T2 holds lock on `Q`, waiting for `P`
→ Neither can proceed.

### Wait-for graph

A directed graph where an edge `T1 → T2` means "T1 is blocked waiting for a resource currently held by T2."

### Cycle detection

DFS traversal maintains a **recursion stack** (nodes on the current path). If DFS reaches a node already in the recursion stack, a back-edge (cycle) is detected. All nodes in the recursion stack at that point are part of the cycle.

### Victim selection

The transaction with the **highest TxnId** (youngest) in the cycle is chosen as the victim. Younger transactions have done less work and are cheaper to abort. The victim is aborted, its locks are released, and the remaining transaction can proceed.

### Scenario 3 cycle

```
T6 holds P, waits for Q  (T7 holds Q) → edge T6 → T7
T7 holds Q, waits for P  (T6 holds P) → edge T7 → T6

Cycle: T6 ↔ T7
Victim: T7 (higher TxnId)
```

After T7 is aborted, T6 can acquire the lock on Q and continue.

---

## 4. How the Three Components Interact

```
TransactionManager
    |
    |-- begin()     -> assigns TxnId, records start_ts (snapshot timestamp)
    |
    |-- read()      -> calls MVCCStore.read(key, start_ts)
    |                  No lock needed (MVCC snapshot read)
    |
    |-- write()     -> calls LockManager.try_acquire(txn_id, key, EXCLUSIVE)
    |                    if granted:
    |                       calls MVCCStore.write(key, value, txn_id, now())
    |                       returns "OK"
    |                    if blocked:
    |                       calls DeadlockDetector.update_wait_for(txn_id, holders)
    |                       calls DeadlockDetector.detect_cycle()
    |                       if cycle found: abort(victim)
    |                         if victim == self: return "ABORTED"
    |                         else: retry acquire, return "OK"
    |                       else: return "BLOCKED"
    |
    |-- commit()    -> LockManager.release_all()
    |                  DeadlockDetector.remove_txn()
    |
    |-- abort()     -> MVCCStore.abort_txn()  (undo writes)
                       LockManager.release_all()
                       DeadlockDetector.remove_txn()
```

---

## 5. Scenario Walkthrough

### Scenario 1 — Basic Read / Write / Commit

T1 acquires exclusive locks on A and B, writes values, commits (locks released). T2 starts after T1 commits — its `start_ts` is after T1's write timestamps, so T2's reads see T1's committed values. Expected: `A=100`, `B=200`.

### Scenario 2 — MVCC Snapshot Isolation

T4 starts before T5 writes. T4's `start_ts` is earlier than T5's write timestamp. Even after T5 commits, T4's snapshot does not include T5's version (because `T5.begin_ts > T4.start_ts`). T4 always reads the pre-T5 value `"original"`.

### Scenario 3 — Deadlock Detection

T6 and T7 each hold a lock and want the other's. When T7 tries to write P (held by T6), the wait-for graph has the cycle `T6 → T7 → T6`. T7 is the youngest (higher id) → T7 is aborted. T6 successfully acquires Q and commits.

### Scenario 4 — Abort and Undo

T8 writes `Y="temp_value"`. Before committing, T8 aborts. `MVCCStore.abort_txn` removes T8's version. T9 starts fresh after the abort — its `read(Y)` finds no visible version → `NOT_FOUND`.
