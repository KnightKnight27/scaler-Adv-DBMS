# Lab 8 — Transaction Manager: Timestamp MVCC + 2PL + Deadlock Detection

**Author:** Md Kaif (24BCS10221)  
**Course:** Advanced Database Management Systems  
**Language:** C++17

A modular C++ program that combines **timestamp-based MVCC**, **Strict Two-Phase Locking**, and **wait-for graph deadlock detection**. Unlike Lab 7 (single-file, thread-based xmin/xmax MVCC), Lab 8 uses a logical clock for snapshots and a synchronous lock model suited for step-by-step viva demos.

---

## Files

| File | Role |
|------|------|
| `db_engine.hpp` | Types, `VersionStore`, `TwoPhaseLockTable`, `DbEngine` API |
| `db_engine.cpp` | MVCC, locking, deadlock logic |
| `main.cpp` | Deadlock demo with two concurrent transactions |
| `CMakeLists.txt` | CMake build script |
| `makefile` | Quick `make run` without CMake |

---

## Build & Run

**Option A — makefile (no CMake required):**

```bash
cd "lab 8"
make run
```

**Option B — CMake:**

```bash
cd "lab 8"
mkdir -p build && cd build
cmake ..
cmake --build .
./lab8_demo
```

---

## Architecture

```
main.cpp
   └── DbEngine
         ├── VersionStore     (timestamp MVCC heap)
         └── TwoPhaseLockTable (strict 2PL + wait-for graph)
```

### 1. Timestamp MVCC

Each row keeps an append-only chain of versions:

```
RowVersion { value, author_tx, commit_time }
```

- **Bootstrap** rows `A=100`, `B=200` with initial commit timestamps.
- **Read** at snapshot `snap_time`: scan newest → oldest, return first version where `commit_time <= snap_time`.
- **Write** does not touch the heap immediately — values go into `staged_writes` and are appended on **commit** with a fresh `commit_time`.

This gives snapshot isolation: a transaction only sees versions committed before its snapshot.

### 2. Strict 2PL

| Phase | Rule |
|-------|------|
| Growing | May acquire S (shared) or X (exclusive) locks |
| Shrinking | Starts at commit/abort — all locks released, no new locks allowed |

- **Read** → S-lock on the key  
- **Write** → X-lock on the key  

If a transaction tries to lock after entering the shrinking phase, it is aborted (2PL violation).

### 3. Deadlock Detection

This lab uses **immediate detection** (no blocking threads):

1. If a lock cannot be granted, record `waiter → blockers` in the wait-for graph.
2. Run DFS cycle detection on the graph.
3. If a cycle exists, **abort the requesting transaction** and release its locks.

---

## Demo Walkthrough (`main.cpp`)

| Step | Action | What happens |
|------|--------|--------------|
| 1 | T1 reads/writes `A=110` | T1 holds X-lock on A |
| 2 | T2 reads/writes `B=190` | T2 holds X-lock on B |
| 3 | T1 writes `B=210` | Blocked — T2 owns B |
| 4 | T2 writes `A=90` | Blocked — T1 owns A → **cycle** → T2 aborted |
| 5 | T1 retries `B=210`, commits | Succeeds after T2 releases locks |
| 6 | T3 reads A and B | Sees committed snapshot |

---

## Sample Output

```
open T1 (snapshot ts=2)
T1 got X-lock on A
T1 staged A = 110
...
=== cross-write deadlock setup ===
T1 blocked on B by T2
T2 blocked on A by T1
cycle in wait-for graph → abort T2
...
=== after deadlock resolution ===
T1 got X-lock on B
T1 staged B = 210
T1 committed
T2 already aborted — skip commit
```

(Version numbers and transaction ids may vary slightly.)

---

## Lab 7 vs Lab 8

| | Lab 7 (`lab 7/txmgr.cpp`) | Lab 8 (this folder) |
|---|---------------------------|---------------------|
| Layout | Single file + makefile | Header / source split + CMake |
| MVCC model | xmin / xmax version chains | Timestamp-ordered commit times |
| Concurrency | Real threads + condition variables | Synchronous step-by-step |
| Deadlock | Detect before thread sleep | Detect on failed lock grant |
| Best for | Showing blocking/wakeup | Tracing logic line-by-line in viva |

---

## Viva Quick Answers

**Q: Why buffer writes until commit?**  
So readers with an older snapshot never see uncommitted data — the new version only gets a `commit_time` at commit.

**Q: What is Strict 2PL?**  
All locks are held until commit/abort; shrinking phase happens once at the end.

**Q: How is deadlock detected?**  
Build a waits-for graph (Tᵢ → Tⱼ if Tᵢ waits for a lock held by Tⱼ). A directed cycle means deadlock.

**Q: Why abort the requester?**  
Simple victim policy: the transaction that completes the cycle when requesting a lock is aborted and its locks are released.

---

## Key Takeaways

1. **Snapshot** = `snap_time` at transaction start; reads ignore versions committed later.
2. **MVCC + 2PL** = MVCC for consistent reads; exclusive locks serialize conflicting writes.
3. **Abort** clears staged writes and releases all locks so other transactions can proceed.
4. Production systems (e.g. PostgreSQL) use richer models, but this lab captures the core ideas in ~200 lines of engine logic.
