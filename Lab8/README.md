# Lab 8 — In-Memory Transaction Manager (MVCC + 2PL)

**Course:** Advanced DBMS — Scaler School of Technology

A header-only C++17 implementation of an in-memory transaction manager combining five concurrency-control primitives found in a real relational engine.

---

## Files

```
Lab8/
├── txn_manager.hpp   # header-only namespace adbms::txn — full implementation
├── main.cpp          # 6-scenario bank-account demo with hard assertions
├── CMakeLists.txt    # C++17 build
└── README.md         # this file
```

---

## Build & Run

```bash
# CMake
cmake -S . -B build && cmake --build build && ./build/txn_manager_demo

# One-liner (g++)
g++ -std=c++17 -Wall -Wextra -o txn_manager_demo main.cpp && ./txn_manager_demo
```

Expected final line: `All transaction-manager checks passed.`

---

## Design

The implementation mirrors the split PostgreSQL uses:

- **Reads use MVCC** — each transaction reads a frozen snapshot captured at `begin()`. Readers never block and never take locks.
- **Writes use Strict 2PL** — a writer takes an exclusive lock on each key it touches and holds it until commit or abort.

### Version Layout

Every key maps to a version chain (`vector<Version>`) sorted by `xmin` (the commit timestamp at which the version became live).

| Field | Meaning |
|-------|---------|
| `value` | Cell payload (`std::string`) |
| `tombstone` | `true` if this version represents a delete |
| `xmin` | Commit timestamp at which the version became visible |
| `xmax` | Commit timestamp at which it was superseded (`0` = still live) |
| `creator` | Id of the transaction that produced it |

**Visibility rule** for snapshot `S`:
```
xmin <= S  AND  (xmax == 0 OR xmax > S)
```

### Lock Manager & Deadlock Detection

Two maps track lock state:

```
xlock_     : key       -> holder_txn_id
waits_for_ : waiter_id -> holder_txn_id
```

On contention, `try_acquire()` adds a `waits_for_` edge then runs a DFS to detect cycles. If a cycle is found, the **youngest transaction** (highest id) in the cycle is aborted as the victim.

### Commit: First-Updater-Wins

Before installing new versions, `commit()` re-checks each written key's latest `xmin`. If `xmin > my_snapshot`, a concurrent transaction already committed a newer version — the current transaction aborts with `SerializationFailure`.

### `gc()` — Vacuum

Prunes dead versions (`xmax != 0`) whose `xmax` is older than the oldest snapshot held by any active transaction. With no live transactions, the current `clock_` is used as the floor.

---

## API

```cpp
adbms::txn::Manager m;

txn_id_t t = m.begin();               // snapshot taken now
std::string v;
m.read(t, "alice", v);                // MVCC read
m.write(t, "alice", "1500");          // 2PL exclusive lock
m.remove(t, "carol");                 // tombstone write
Result r = m.commit(t);               // Ok / SerializationFailure / Aborted
m.abort(t);                           // release locks, discard buffer

m.gc();                               // prune dead versions

// Introspection
m.state_of(t);        // Active / Committed / Aborted
m.last_victim();      // id of last deadlock victim
m.live_txn_count();
m.lock_count();
m.version_count();
m.dump_chain("alice");
m.check_invariants(); // "" when healthy
```

Return values use `enum class Result { Ok, NotFound, LockWait, Aborted, SerializationFailure }`.

---

## Demo Scenarios

The driver seeds three accounts (`alice=1000`, `bob=500`, `carol=750`) and runs six scenarios:

| # | Scenario | Proves |
|---|----------|--------|
| 1 | Reader snapshots `alice=1000`; writer commits `alice=1500`; reader still sees `1000` | MVCC snapshot isolation |
| 2 | Old snapshot before `delete carol`; new snapshot after; old still sees row | Tombstone visibility |
| 3 | T1 holds X-lock on `bob`; T2's write returns `LOCK_WAIT`; T2 succeeds after T1 aborts | Strict 2PL |
| 4 | T_AB locks `alice`, T_BA locks `bob`, then each tries to grab the other's account | Waits-for cycle detection — youngest txn aborted |
| 5 | Two txns snapshot same `alice=1400`; first commit wins; second gets `SERIALIZATION_FAILURE` | First-updater-wins |
| 6 | After all scenarios, `gc()` reclaims 6 of 9 versions; live values survive | Vacuum correctness |

`check_invariants()` is called after every scenario to verify:
- Every X-lock holder is an `Active` transaction
- Version chains have strictly-increasing `xmin`
- Every superseded version has `xmin < xmax`
- Only the last version per chain may have `xmax = 0`

---

## Complexity

| Operation | Time |
|-----------|------|
| `begin()` | O(1) |
| `read(key)` | O(v) scan of version chain |
| `write(key)` | O(t) deadlock DFS in worst case |
| `commit()` | O(n) first-updater check + version installs |
| `abort()` | O(n) lock release |
| `gc()` | O(total versions) — single pass |
