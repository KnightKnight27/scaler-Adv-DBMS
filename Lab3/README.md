# ClockSweep Cache

A generic, thread-safe in-memory cache implementing the **Clock Sweep page replacement algorithm**, written in C++17.

---

## What problem does this solve?

When a database engine reads pages from disk, it keeps recently used pages in a fixed-size memory buffer (a cache) to avoid slow repeated disk reads. But memory is limited — at some point a page has to be evicted to make room for a new one. The question is: *which page do you throw out?*

The **Clock Sweep algorithm** answers that question. It is the same algorithm used by PostgreSQL's buffer manager.

---

## How the Clock Sweep algorithm works

Picture the cache slots arranged in a circle, with a hand (like a clock hand) pointing at one of them.

Every slot has a **reference bit** (`ref = 0` or `ref = 1`).

- When a page is accessed (`get`), its ref bit is set to **1** — "this was used recently."
- When space is needed, the hand sweeps forward:
  - If `ref = 1` → clear it to 0 ("second chance") and move on.
  - If `ref = 0` → evict this slot. It hasn't been used since the last time the hand passed.

This means hot entries (used often) keep getting their ref bits refreshed and survive. Cold entries (not used) have their ref bits cleared and eventually get evicted.

```
Before eviction (all full, hand at slot 0):
  [0] val=10  ref=1  <-- hand
  [1] val=20  ref=1
  [2] val=30  ref=1       <- 10 and 20 were recently get()-ed
  [3] val=40  ref=1

put(50) triggers clock sweep:
  hand at 0: ref=1 → clear to 0, advance
  hand at 1: ref=1 → clear to 0, advance
  hand at 2: ref=1 → clear to 0, advance
  hand at 3: ref=1 → clear to 0, advance
  hand at 0: ref=0 → EVICT slot 0, insert 50 here

After:
  [0] val=50  ref=1
  [1] val=20  ref=0
  [2] val=30  ref=0
  [3] val=40  ref=0
```

---

## Code structure

### `Frame<T>` struct

```cpp
template<typename T>
struct Frame {
    T value;
    bool occupied   = false;
    bool referenced = false;
};
```

One slot in the circular buffer. Holds the cached value plus its two flags.

### `ClockSweep<T>` class

| Member | Purpose |
|---|---|
| `frames_` | `std::vector<Frame<T>>` — the circular buffer |
| `index_` | `std::unordered_map<T, size_t>` — maps each key to its slot for O(1) lookup |
| `clockHand_` | Current position of the sweep hand |
| `mutex_` | Protects all shared state from concurrent access |
| `bgClockThread_` | Background thread that periodically ages entries |
| `stopBg_` | Atomic flag to signal the background thread to exit |

### `get(key)`

1. Lock the mutex.
2. Look up the key in `index_`. If not found → throw `std::out_of_range`.
3. If found → set `ref = true` on that frame, return the value.

### `put(key)`

1. Lock the mutex.
2. If key already exists → just refresh its ref bit, return.
3. If there is a free slot → use it directly.
4. If cache is full → call `findOrEvict()` to run the clock sweep and get a free slot.
5. Write the new value into the slot, update `index_`.

### `findOrEvict()`

The core of the algorithm:

```
loop:
  look at frame at clockHand_
  if ref == 1:
      clear ref to 0
      advance hand
  else:
      remove from index_
      mark slot as unoccupied
      advance hand
      return this slot
```

### Background thread

Runs every N seconds. Walks all frames and clears ref bits on occupied entries. This "ages" entries over time — if a page hasn't been accessed in N seconds its ref bit will be 0 by the time the clock hand reaches it, making it a quick eviction candidate.

---

## Thread safety

A single `std::mutex` (`mutex_`) guards all reads and writes to `frames_`, `index_`, and `clockHand_`. Both the foreground (`get`/`put`) and background sweep acquire this lock before touching shared state. This prevents data races at the cost of some lock contention — acceptable for a teaching implementation.

---

## Generics and future `Page` support

The class is templated on `T`:

```cpp
ClockSweep<int>         intCache(4);
ClockSweep<std::string> strCache(8);
```

The only requirement on `T` is that `std::unordered_map<T, ...>` compiles — meaning `T` needs `std::hash<T>` and `operator==`. To use it with a custom `Page` type later:

```cpp
// 1. define your Page type
struct Page { int pageId; /* ... */ };

// 2. add a hash specialisation
namespace std {
    template<> struct hash<Page> {
        size_t operator()(const Page& p) const {
            return std::hash<int>{}(p.pageId);
        }
    };
}

// 3. done
ClockSweep<Page> bufferPool(256);
```

---

## Building

```bash
mkdir build && cd build
cmake ..
make
./db_engine
```

Requires a C++17 compiler and pthreads (handled automatically by `find_package(Threads)`).