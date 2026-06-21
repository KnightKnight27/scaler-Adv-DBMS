# Lab 8 — In-Memory Transaction Manager (MVCC + Strict 2PL)

**Course:** Advanced DBMS — Scaler School of Technology
**Student:** Aman Yadav
**Roll No:** 24BCS10183
**Class:** B
**Year:** 2nd Year

A header-only C++17 transaction manager that combines the concurrency-control
primitives found in a real relational engine: MVCC reads, Strict 2PL writes,
waits-for cycle deadlock detection, first-updater-wins serialization checks,
and a vacuum routine.

---

## Files

```
lab8/
├── txn_manager.hpp   # header-only namespace adbms::txn — full implementation
├── main.cpp          # 6-scenario bank-account demo with hard assertions
├── CMakeLists.txt    # C++17 build
└── README.md         # this file
```

---

## Build & Run

```bash
# CMake
cmake -S . -B build && cmake --build build && ./build/lab8_txn_manager

# One-liner (g++)
g++ -std=c++17 -Wall -Wextra -o lab8_txn_manager main.cpp && ./lab8_txn_manager
```

Expected final line: `All transaction-manager checks passed.`

---

## Design

The implementation mirrors how PostgreSQL splits reads and writes:

- **Reads use MVCC** — each transaction freezes a snapshot timestamp at
  `begin()`. Readers walk the version chain and pick the version visible to
  that snapshot. Readers never block and never take locks.
- **Writes use Strict 2PL** — a writer takes an exclusive (X) lock on each
  key it touches and holds it until commit or abort.

### Version Layout

Every key maps to a chain (`vector<Version>`) ordered by `xmin`:

| Field       | Meaning                                                |
|-------------|--------------------------------------------------------|
| `value`     | Cell payload                                           |
| `tombstone` | `true` if this version represents a delete            |
| `xmin`      | Commit timestamp at which the version became visible   |
| `xmax`      | Commit timestamp at which it was superseded (`0` = live) |
| `creator`   | Id of the transaction that produced it                 |

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

On contention, `write()` adds a `waits_for_` edge and runs a DFS to detect
cycles. If a cycle is found, the **youngest transaction** (highest id) in the
cycle is aborted as the victim.

### Commit: First-Updater-Wins

Before installing pending versions, `commit()` re-checks each written key's
latest `xmin`. If `xmin > my_snapshot`, a concurrent transaction has already
landed a newer version — the current transaction aborts with
`SerializationFailure`.

### `gc()` — Vacuum

Prunes versions whose `xmax != 0` and is older than the oldest snapshot held
by any active transaction. The tail (latest) version of each chain is always
retained so live values survive the sweep.

---

## API

```cpp
adbms::txn::Manager m;

txn_id_t t = m.begin();                 // snapshot captured now
std::string v;
m.read(t, "alice", v);                  // MVCC read
m.write(t, "alice", "1500");            // 2PL X-lock
m.remove(t, "carol");                   // tombstone write
Result r = m.commit(t);                 // Ok / SerializationFailure / Aborted
m.abort(t);                             // release locks, discard buffer
m.gc();                                 // prune dead versions

// Introspection
m.state_of(t);                          // Active / Committed / Aborted
m.last_victim();                        // id of last deadlock victim
m.live_txn_count();
m.lock_count();
m.version_count();
m.check_invariants();                   // "" when healthy
```

`Result` values: `Ok`, `NotFound`, `LockWait`, `Aborted`, `SerializationFailure`.

---

## Demo Scenarios

The driver seeds three accounts (`alice=1000`, `bob=500`, `carol=750`) and
runs six scenarios:

| # | Scenario                                                                      | Proves                          |
|---|-------------------------------------------------------------------------------|---------------------------------|
| 1 | Reader snapshots `alice=1000`; writer commits `alice=1500`; reader still sees `1000` | MVCC snapshot isolation         |
| 2 | Old snapshot before `delete carol`; new snapshot after; old still sees row    | Tombstone visibility            |
| 3 | T1 holds X-lock on `bob`; T2's write returns `LockWait`; succeeds after T1 aborts | Strict 2PL                      |
| 4 | T_AB locks `alice`, T_BA locks `bob`, then each grabs the other               | Waits-for cycle — youngest aborted |
| 5 | Two txns snapshot same `alice`; first commit wins; second `SerializationFailure` | First-updater-wins              |
| 6 | After all scenarios, `gc()` reclaims dead versions; live values survive       | Vacuum correctness              |

`check_invariants()` runs after every scenario to verify:

- Every X-lock holder is an `Active` transaction
- Version chains have strictly-increasing `xmin`
- Every superseded version has `xmin < xmax`
- Only the tail version per chain may have `xmax = 0`

---

## Complexity

| Operation    | Time                                |
|--------------|-------------------------------------|
| `begin()`    | O(1)                                |
| `read(key)`  | O(v) scan of version chain          |
| `write(key)` | O(t) deadlock DFS in the worst case |
| `commit()`   | O(n) first-updater check + installs |
| `abort()`    | O(n) lock release                   |
| `gc()`       | O(total versions) single pass       |
