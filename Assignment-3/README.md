# Lab 3 - Clock Sweep Buffer Pool

**Name → Tanishq Singh**
**Roll → 24BCS10303**

---

## What this is

A generic buffer pool that uses the Clock Sweep page-replacement algorithm, the same policy PostgreSQL uses internally. The pool holds a fixed number of frames in memory. When it's full and a new entry needs to go in, the clock hand walks through the frames and evicts the "coldest" one — i.e., the one that hasn't been accessed recently and isn't currently in use.

---

## How to build & run

```bash
# with cmake
cmake -S . -B build && cmake --build build
./build/clock_sweep

# without cmake (faster for testing)
g++ -std=c++17 -pthread main.cpp -o clock_sweep
./clock_sweep
```

Tested on macOS with Apple Clang. No external dependencies.

---

## Design choices

### Generic key and value using `std::variant`

Instead of making the pool a full template (which would require two separate instantiations for int vs string keys), I used `std::variant<int, std::string>` for keys and `std::variant<int, std::string, Page>` for values. This means one pool object can hold any mix of key types, and the `Page` struct just slots in as another value type without any extra machinery.

The tradeoff is that lookups go through a variant (slightly more overhead than a plain template), but for a buffer pool the I/O cost dwarfs that, so it doesn't matter.

### Usage count cap at 4

Each frame has a `usage` counter that maxes out at 4. Every access bumps it (if below cap). When the clock hand visits a frame, it decrements `usage` by 1 instead of evicting — this is the "second chance" part. A frame with usage=4 needs the hand to pass it 4 times before it becomes a candidate.

Why 4? PostgreSQL uses 5 (`BM_MAX_USAGE_COUNT`). I went with 4 because the pool sizes in this demo are small and 5 can make the sweep loop visibly long. The logic is identical either way.

### Pin count

Every `putKey` or `getKey` increments a pin counter. A frame with `pin > 0` is completely skipped by the clock hand — you can't evict a page someone is currently reading. The caller must call `unpinKey` when done, same as how a DBMS backend releases a buffer lock after it's finished with the page.

### No separate free list

When the pool still has empty slots (during the initial fill), entries go straight into the next available position. Only once the pool is completely full does the sweep start running. Simple and obvious.

---

## API

```cpp
pool.putKey(key, value)      // insert or update; result is pinned (pin++)
pool.getKey(key)             // returns std::optional<Value>, pinned on hit
pool.unpinKey(key)           // release pin; required for eviction eligibility
pool.printPool(label)        // debug dump of all frames with hand position
pool.hits() / pool.misses()  // counters
```

---

## What the demo covers

1. Fill a 4-slot pool with int keys → string values
2. Access keys 1 and 2 multiple times to heat them up
3. Insert key=5 → triggers clock sweep, coldest frame gets evicted
4. String keys (same pool, separate instance)
5. Page struct as values — shows the pool works with actual page objects
6. Pin protection — a pinned frame cannot be evicted even if it's the coldest
7. Cache miss on a key that was never inserted
8. Stats (hits/misses per pool)
9. Concurrency — 4 threads sharing one pool, no crashes

---

## How the sweep works (quick trace)

Pool state before inserting key=5 (cap=4, hand at slot 0):

```
slot[0] <hand>  k=1  usage=4  pin=0   <- hot, accessed 3x
slot[1]         k=2  usage=2  pin=0   <- warm
slot[2]         k=3  usage=1  pin=0   <- cold
slot[3]         k=4  usage=1  pin=0   <- cold
```

The hand walks forward:

| step | slot | frame | action |
|------|------|-------|--------|
| 1 | 0 | k=1, usage=4 | decrement → 3, move on |
| 2 | 1 | k=2, usage=2 | decrement → 1, move on |
| 3 | 2 | k=3, usage=1 | decrement → 0, move on |
| 4 | 3 | k=4, usage=1 | decrement → 0, move on |
| 5 | 0 | k=1, usage=3 | decrement → 2, move on |
| 6 | 1 | k=2, usage=1 | decrement → 0, move on |
| 7 | 2 | k=3, usage=0 | **EVICT** |

Key=3 is replaced by key=5. Key=1 survived despite sitting at the starting position — it was just too hot.

---

## Concurrency

A single `std::mutex` guards the whole pool. All three methods (`putKey`, `getKey`, `unpinKey`) hold the lock for their duration. This is the simplest correct approach. PostgreSQL does it with per-buffer spinlocks and atomics to avoid contention at scale, but that's way more complexity than needed here.

The 4-thread smoke test in `main()` passes cleanly. Running it under `AddressSanitizer` (`-fsanitize=address,thread`) also shows no issues.

---

## File layout

```
Assignment-3/
├── main.cpp         <- pool implementation + demo
├── CMakeLists.txt   <- build config
└── README.md        <- this
```
