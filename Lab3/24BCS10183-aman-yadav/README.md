# Lab 3 — Clock Sweep Buffer Cache (C++)

**Name.** Aman Yadav
**Roll No.** 24BCS10183
**Class.** B (2nd year)

## Goal

Implement the **clock sweep** (a.k.a. *second-chance*) page-replacement
algorithm in C++ as a templated cache, intended as the eviction policy for a
database storage-buffer manager. The cache must:

- Be generic (`template<typename T>`) so it can later hold `Page` objects.
- Expose `getKey` / `putKey` on a fixed-capacity buffer.
- Run a **background thread** that periodically performs the clock sweep.
- Be safe to call concurrently from a foreground worker and the sweeper.

## The algorithm

Each slot in the buffer carries a single **reference bit** (`refBit`). The
buffer is treated as a circular array with a moving **hand** pointer.

- `getKey(key)` (hit) sets the slot's `refBit = 1`.
- `putKey(key)`:
  - If the key is already cached, set `refBit = 1` and return.
  - If a slot is free, place the new entry there and advance the hand.
  - Otherwise run **find-victim** starting at the hand:
    - if `refBit == 0` → evict this slot (the victim).
    - if `refBit == 1` → clear it to `0` (give it a "second chance") and advance.
  - On insertion of a fresh entry, `refBit = 1` so it isn't immediately
    re-evicted on the next sweep.
- A **background thread** wakes every `sweepInterval` and clears every
  occupied slot's `refBit` to `0`. This is the periodic *aging* step: items
  that aren't touched again before the next sweep become eligible victims.

The combination of (a) on-demand sweep during a full-cache `put` and (b)
periodic aging by the background thread is the standard implementation found
in production buffer managers (e.g. PostgreSQL's `BufferStrategyControl`).

## Files

| File | Purpose |
|---|---|
| `main.cpp` | The `ClockSweepCache<T>` class plus a `main()` with three demos. |
| `CMakeLists.txt` | CMake build (C++17, pthreads, warnings). |

## Public API

```cpp
template <typename T>
class ClockSweepCache {
public:
    explicit ClockSweepCache(std::size_t capacity,
                             std::chrono::milliseconds sweepInterval = 500ms);
    ~ClockSweepCache();                  // joins the background thread

    T    getKey(const T& key);           // hit: returns key + sets refBit
                                         // miss: returns T{}
    void putKey(const T& key);           // insert or refresh; evicts when full
    bool contains(const T& key);         // diagnostics, no side-effect on refBit
    std::size_t size();
    std::size_t capacity() const;

    void debugPrint(const std::string& label);
};
```

Method names `getKey` / `putKey` match the instructor's skeleton in
`storage_buffer/main.cpp` at the repo root.

## Concurrency model

- A single `std::mutex` (`mu_`) guards `frames_`, `index_`, and `hand_`.
- The background sweeper takes `mu_` only while it clears ref bits, then
  releases it and waits on a `std::condition_variable` (`cv_`).
- Shutdown is signalled by setting `stop_ = true` under the lock and calling
  `cv_.notify_all()` so the destructor doesn't block for a full sweep
  interval before joining.
- The class is non-copyable.

## Build

### With CMake (recommended for the grader)

```bash
cmake .
make
./db_engine
```

### Without CMake (plain compiler invocation)

```bash
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread main.cpp -o db_engine
./db_engine
```

Both produce the same binary. Tested with Apple clang on macOS arm64.

## Sample run

Captured from `./db_engine`:

```
=== Demo 1: ClockSweepCache<int>, capacity=4, sweep=300ms ===
[put 1] hand=1 | slot0=1(ref=1)  slot1=_  slot2=_  slot3=_
[put 2] hand=2 | slot0=1(ref=1)  slot1=2(ref=1)  slot2=_  slot3=_
[put 3] hand=3 | slot0=1(ref=1)  slot1=2(ref=1)  slot2=3(ref=1)  slot3=_
[put 4 (cache full)] hand=0 | slot0=1(ref=1)  slot1=2(ref=1)  slot2=3(ref=1)  slot3=4(ref=1)

-- sleep 400ms so the background sweep ages all ref bits to 0 --
[after bg sweep] hand=0 | slot0=1(ref=0)  slot1=2(ref=0)  slot2=3(ref=0)  slot3=4(ref=0)
[get 2 (hit, refBit -> 1)] hand=0 | slot0=1(ref=0)  slot1=2(ref=1)  slot2=3(ref=0)  slot3=4(ref=0)
[get 4 (hit, refBit -> 1)] hand=0 | slot0=1(ref=0)  slot1=2(ref=1)  slot2=3(ref=0)  slot3=4(ref=1)

-- put 5: clock should evict the first slot with refBit=0 --
[put 5] hand=1 | slot0=5(ref=1)  slot1=2(ref=1)  slot2=3(ref=0)  slot3=4(ref=1)

-- put 6: another eviction via clock sweep --
[put 6] hand=3 | slot0=5(ref=1)  slot1=2(ref=0)  slot2=6(ref=1)  slot3=4(ref=1)

contains(2)=true  contains(1)=false  contains(5)=true
size=4 / capacity=4

=== Demo 2: ClockSweepCache<std::string>, capacity=3 ===
[filled] hand=0 | slot0=alpha(ref=1)  slot1=bravo(ref=1)  slot2=charlie(ref=1)
[get alpha (refBit -> 1)] hand=0 | slot0=alpha(ref=1)  slot1=bravo(ref=1)  slot2=charlie(ref=1)
[after bg sweep] hand=0 | slot0=alpha(ref=0)  slot1=bravo(ref=0)  slot2=charlie(ref=0)
[get alpha again (only alpha has ref=1)] hand=0 | slot0=alpha(ref=1)  slot1=bravo(ref=0)  slot2=charlie(ref=0)
[put delta (evicts bravo — first refBit=0 slot)] hand=2 | slot0=alpha(ref=0)  slot1=delta(ref=1)  slot2=charlie(ref=0)
[put echo] hand=0 | slot0=alpha(ref=0)  slot1=delta(ref=1)  slot2=echo(ref=1)
contains(alpha)=true  contains(bravo)=false  contains(charlie)=false

=== Demo 3: putKey on existing key just refreshes refBit ===
[filled] hand=0 | slot0=100(ref=1)  slot1=200(ref=1)  slot2=300(ref=1)
[after bg sweep (all aged)] hand=0 | slot0=100(ref=0)  slot1=200(ref=0)  slot2=300(ref=0)
[put 200 again (existing key, refBit -> 1)] hand=0 | slot0=100(ref=0)  slot1=200(ref=1)  slot2=300(ref=0)
size=3 (still 3, no growth)

All demos finished.
```

## Walking through Demo 1 (the textbook case)

Initial fill: `[1(1), 2(1), 3(1), 4(1)]`, hand=0.

1. Background sweep fires once the cache has been idle for ~300 ms and clears
   every refBit → `[1(0), 2(0), 3(0), 4(0)]`.
2. `getKey(2)` and `getKey(4)` mark slots 1 and 3 as recently used →
   `[1(0), 2(1), 3(0), 4(1)]`.
3. `putKey(5)` — cache is full, so `pickVictim` runs from hand=0:
   - slot 0 has refBit=0 → it is the victim. `1` is evicted, `5` lands at slot 0.
4. `putKey(6)` — runs from hand=1:
   - slot 1 has refBit=1 → clear it to 0, advance (second chance for `2`).
   - slot 2 has refBit=0 → victim. `3` is evicted, `6` lands at slot 2.

This is the textbook second-chance behaviour: recently-used items (`2`, `4`)
survived, the coldest items (`1`, `3`) were evicted.

## Extending to pages

The class is `template<typename T>`, so the same code can hold `Page`
objects once the storage layer is built. The only assumption on `T` is that
it is hashable (`std::unordered_map<T, ...>`) and copy/move-constructible.
For a real `Page`, a `<PageId, Page>` key/value variant is a one-line change:
swap the inner `Frame { T key; ... }` for `Frame { Key k; Value v; ... }` and
update `index_` to `unordered_map<Key, size_t>`.
