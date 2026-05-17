# Lab 3 — Clock Sweep Algorithm (C++)

**Name:** Vibhuti Bhatnagar
**Role Number:** 24BCS10288

A C++17 implementation of the **Clock Sweep** buffer-replacement algorithm used in PostgreSQL's buffer manager (`src/backend/storage/buffer/freelist.c`). The pool is generic over key/value types, thread-safe, and demonstrates pinning, eviction, and "second-chance" semantics.

---

## 1. What the algorithm does

A database buffer pool can only hold a limited number of pages in memory. When a new page has to be loaded and every slot is occupied, the manager must pick a victim to evict. The **Clock Sweep** policy approximates LRU at much lower bookkeeping cost.

Every frame in the pool carries two pieces of metadata:

| Field         | Meaning |
|---------------|---------|
| `usage_count` | Incremented on every access; capped at `MAX_USAGE_COUNT = 5`. |
| `pin_count`   | Number of callers currently holding the page; eviction is forbidden while > 0. |

A single **clock hand** rotates through the frames. When a victim is needed:

```
while True:
    f = frames[hand]
    hand = (hand + 1) % N
    if f.pin_count > 0:        continue        # pinned — skip entirely
    if f.usage_count > 0:      f.usage_count--; continue   # second chance
    return f                                    # unpinned + cold -> evict
```

Two design properties fall out of this loop:

1. **Hot pages survive.** A page that was touched many times has a high `usage_count`; the hand has to pass over it several revolutions before it becomes evictable.
2. **The algorithm always terminates.** Because every visit decrements `usage_count`, at most `MAX_USAGE_COUNT + 1` full revolutions over the unpinned subset are enough to drive some frame to zero. (If *every* frame is pinned, eviction is genuinely impossible — the implementation throws.)

This is cheaper than true LRU because there is no per-access list reordering: an access just bumps an integer.

---

## 2. Files

```
Lab3/24BCS10288 Vibhuti Bhatnagar/
├── CMakeLists.txt   # CMake config (C++17, pthreads, -Wall -Wextra)
├── main.cpp         # ClockSweepBufferPool<K,V> + demo + concurrency test
└── README.md        # this file
```

## 3. Build & run

### With CMake (matches the rest of the course)

```bash
cmake -S . -B build
cmake --build build
./build/db_engine
```

### Without CMake (one-liner)

```bash
clang++ -std=c++17 -Wall -Wextra -Wpedantic -pthread main.cpp -o db_engine
./db_engine
```

Both produce the same executable. Tested on macOS (Apple clang 21.0).

---

## 4. API

`ClockSweepBufferPool<Key, Value>` (header-only, in `main.cpp`):

| Method | Behavior |
|---|---|
| `pool.put(k, v)`     | Loads (or updates) page `k`. If the pool is full, runs the clock sweep to evict a victim. Returns the new page **pinned**. |
| `pool.get(k)`        | Returns `std::optional<Value>`. On hit, bumps `usage_count` and pins. |
| `pool.unpin(k)`      | Releases one pin; required before a page becomes eligible for eviction. |
| `pool.dump(label)`   | Prints all frames in clock-hand order — useful for tracing. |
| `pool.hits()` / `pool.misses()` / `pool.size()` | Counters. |

Concurrency: a single `std::mutex` guards the pool. The class is safe to share between threads (verified by a 4-thread × 200-iteration smoke test in `main()`).

---

## 5. Walk-through of the demo output

Running `./db_engine` executes seven scenarios. The interesting ones:

### Scenario 3 — first eviction picks the coldest page

Before the insert:

```
[0] <-hand  key=10  usage=3  pin=0     # hot, accessed 3×
[1]         key=20  usage=2  pin=0     # warm, accessed 2×
[2]         key=30  usage=1  pin=0     # cold
[3]         key=40  usage=1  pin=0     # cold
```

`pool.put(50, …)` runs the sweep:

| Step | Hand | Frame   | Action               |
|------|------|---------|----------------------|
| 1    | 0    | 10 (u=3) | dec → 2, advance     |
| 2    | 1    | 20 (u=2) | dec → 1, advance     |
| 3    | 2    | 30 (u=1) | dec → 0, advance     |
| 4    | 3    | 40 (u=1) | dec → 0, advance     |
| 5    | 0    | 10 (u=2) | dec → 1, advance     |
| 6    | 1    | 20 (u=1) | dec → 0, advance     |
| 7    | 2    | 30 (u=0) | **EVICT** ← victim   |

Result (hand left at slot 3, page 30 replaced by 50):

```
[0]         key=10  usage=1  pin=0
[1]         key=20  usage=0  pin=0
[2]         key=50  usage=1  pin=0
[3] <-hand  key=40  usage=0  pin=0
```

This is exactly what the program prints. The hottest page (10) survived even though it sat at the very position the hand started from.

### Scenario 5 — pinned pages are never evicted

After re-pinning 10 (`pool.get(10)` without `unpin`), the state was:

```
[0] <-hand  key=10  usage=2  pin=1   # PINNED
[1]         key=20  usage=0  pin=0
[2]         key=50  usage=1  pin=0
[3]         key=60  usage=1  pin=0
```

`pool.put(70, …)` walks the hand:

| Step | Hand | Frame   | Action               |
|------|------|---------|----------------------|
| 1    | 0    | 10 (pinned) | skip               |
| 2    | 1    | 20 (u=0)    | **EVICT**          |

So `70` lands in slot 1, replacing `20`, even though `20` was not the absolute coldest (page 10 had higher usage but was pinned). The trace confirms the implementation respects pin counts.

### Scenario 7 — concurrency smoke test

Four threads each issue 200 `put / unpin / get / unpin` operations on a shared 8-slot pool with overlapping keys. Final count from the last run:

```
concurrency test done.  hits=754  misses=46
```

No crashes, no data corruption, no deadlocks. (Compiling with `-fsanitize=thread` produced no TSAN warnings either.)

---

## 6. Comparison with PostgreSQL

| Aspect              | This implementation                       | PostgreSQL 16 (`freelist.c`)              |
|---------------------|-------------------------------------------|-------------------------------------------|
| Replacement policy  | Clock sweep                               | Clock sweep                               |
| Max usage count     | `kMaxUsageCount = 5`                      | `BM_MAX_USAGE_COUNT = 5`                  |
| Hand state          | `std::size_t hand_`                       | `nextVictimBuffer` in `BufferStrategyControl` |
| Eviction primitive  | `pickVictim()` mutates `usage_count`       | `ClockSweepTick()` does the same          |
| Synchronisation     | One `std::mutex` per pool                 | Atomic ops + spinlocks per buffer header  |
| Free-list           | None — every miss runs the sweep          | Maintains a free-list for first allocs    |

The biggest simplification here is the single mutex. Postgres uses per-buffer spinlocks and an atomic increment-then-clamp loop on `BM_USAGE_COUNT` to keep the hot path lock-free; that complexity isn't useful at this scale.

---

## 7. Compared with the reference (PR #259)

| Aspect | Reference (Lab3 #259) | This submission |
|--------|----------------------|------------------|
| Decay of usage_count | Background thread every 2 s | Decremented by the clock hand on demand (PG-style) |
| Pin model | `pinned` boolean, auto-cleared by timer | `pin_count` integer with explicit `unpin()` (re-entrant safe) |
| Termination guarantee | Implicit (relies on timer + manual unpin) | Explicit safety bound + throws if all pinned |
| Demo | Single scenario | 7 scenarios + 4-thread concurrency test with `pool.dump()` traces |
| API | `getKey / putKey` only | `get / put / unpin / dump / hits() / misses()` |

The on-demand decay matches what PostgreSQL actually does in `StrategyGetBuffer()`, so the hand makes progress only when a victim is needed — no idle CPU spent decaying counts that nobody is going to read.

---

## 8. Reproducing the test runs

```bash
clang++ -std=c++17 -pthread main.cpp -o db_engine && ./db_engine
# expected:
#   ================ 1) Fill the pool ================
#   ...
#   total hits   = 4
#   total misses = 0
#   pool size    = 4
#   ================ 7) Concurrency smoke test ================
#   ...
#   concurrency test done.  hits=754  misses=46     (numbers vary per run)
```
