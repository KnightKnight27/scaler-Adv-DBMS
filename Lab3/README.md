# Lab 3 — Clock Sweep Algorithm (C++)

Implementation of the **clock sweep** page-replacement algorithm in C++, as
a generic, thread-safe cache. This is a component of the storage buffer for
the database engine we're building in the course.

## Algorithm

Each cached frame carries a `referenceBit`. A circular "clock hand" advances
through the frames:

- `get(key)` — if the key is present, set its `referenceBit = true` and
  return the value; otherwise return a miss.
- `put(key)` — if the cache is full, sweep: starting at the hand, if the
  current frame's `referenceBit` is `true`, clear it and advance; if it's
  `false`, evict that frame and place the new key there.
- A **background thread** wakes every `sweepInterval` ms and advances the
  hand one slot, clearing a reference bit if set. This is the "aging" pass
  (analogous to PostgreSQL's `bgwriter`).

This gives an approximate-LRU eviction policy with O(1) amortized cost and
no per-access bookkeeping beyond a single bit.

## Files

- `main.cpp` — `ClockSweep<T>` template class + test driver in `main()`.
- `CMakeLists.txt` — CMake build (requires C++17, threads).

## Public API

```cpp
template <typename T>
class ClockSweep {
public:
    explicit ClockSweep(std::size_t maxCacheSize,
                        std::chrono::milliseconds sweepInterval = 500ms);
    std::optional<T> get(const T& key);   // marks referenceBit on hit
    void             put(const T& key);   // inserts, evicting via sweep if full
    void             dump(std::ostream&) const;  // debug print
};
```

Designed to be specialised later to `ClockSweep<Page*>` for the buffer pool.

## Build

Requires CMake ≥ 3.10 and a C++17 compiler (g++, clang++, or MSVC).

```bash
cd Lab3
cmake -S . -B build
cmake --build build
./build/db_engine          # Linux/macOS/WSL
.\build\db_engine.exe      # Windows
```

## Sample output

```
-- inserting 1..4 (fills cache) --
[ *1(1) _ _ _ ]  hand=0
[ *1(1) 2(1) _ _ ]  hand=0
[ *1(1) 2(1) 3(1) _ ]  hand=0
[ *1(1) 2(1) 3(1) 4(1) ]  hand=0

-- get(2): marks 2 as referenced --
get(2) -> 2
[ *1(1) 2(1) 3(1) 4(1) ]  hand=0

-- put(5): triggers sweep, should evict an unreferenced frame --
[ 5(1) 2(0) 3(0) 4(0) *]  hand=0
```

(Exact frame contents and hand position vary with the background sweep
schedule; what matters is that frames with `referenceBit=1` get a "second
chance" before eviction.)

## Design notes

- A single `std::mutex` guards `frames`, `index`, `hand`, and `occupiedCount`.
- The destructor flips `running=false`, notifies the condvar, and joins the
  background thread — no detached threads, no leaks.
- `std::unordered_map<T, size_t>` gives O(1) key lookup into the ring.
- Templated on `T`, so any hashable key type works (`int`, `std::string`, or
  later a `Page` identifier).
