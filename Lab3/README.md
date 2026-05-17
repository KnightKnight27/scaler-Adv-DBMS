# ClockSweep Cache 

A generic, thread-safe in-memory cache implementing the **Clock Sweep page replacement algorithm**, written in C++17. Capacity is fixed at compile time; all storage lives on the stack.

---

## What problem does this solve?

A database engine keeps recently read pages in a fixed-size memory buffer to avoid hitting disk repeatedly. When the buffer is full and a new page arrives, something must be evicted. The **Clock Sweep algorithm** decides what goes — it is the eviction policy used by PostgreSQL's shared buffer manager.

---

## How the Clock Sweep algorithm works

The cache slots form a logical circle. A hand sweeps around it, inspecting each slot's **reference bit**:

- `ref = 1` → this entry was used recently. Give it a **second chance**: clear the bit and move on.
- `ref = 0` → this entry has been cold since the hand last passed. **Evict it.**

Any `get()` call sets the ref bit back to 1, so actively used entries keep surviving. Entries nobody touches eventually have their bit cleared and get swept out.

```
Cache full, hand at slot 0. put(5) arrives:

  Step 1 — hand at 0: ref=1 (was get()-ed) → clear, advance
  Step 2 — hand at 1: ref=1               → clear, advance
  Step 3 — hand at 2: ref=1               → clear, advance
  Step 4 — hand at 3: ref=1               → clear, advance
  Step 5 — hand at 0: ref=0               → EVICT, place 5 here

Result:
  [0] val=5   ref=1
  [1] val=2   ref=0
  [2] val=3   ref=0
  [3] val=4   ref=0
```

---

## Code structure

### Template parameters

```cpp
template<typename T, std::size_t N = MAX_CAPACITY>
class ClockSweep
```

- `T` — the value type. Needs `operator==` and a copy constructor. No hash required.
- `N` — maximum number of slots, fixed at **compile time** (default 64). The backing arrays are `std::array<_, N>`, so no heap allocation happens for the buffer itself.

A runtime `capacity_` parameter (passed to the constructor) can be smaller than `N`, letting you cap the cache without recompiling.

### Struct-of-arrays layout

Instead of one array of `{value, occupied, refbit}` structs, the data is split into three separate arrays:

```cpp
std::array<T,       N>  keys_;      // the cached values
std::array<bool,    N>  occupied_;  // is this slot in use?
std::array<uint8_t, N>  refbit_;    // reference bit (0 or 1)
```

**Why?** When the background sweep runs, it only needs to touch `refbit_`. With struct-of-arrays, that array is contiguous in memory and fits in fewer cache lines. With an array-of-structs, the sweep would stride through the full `T` objects just to reach the bit — wasting memory bandwidth.

`refbit_` is `uint8_t` rather than `bool` intentionally: it can be extended to a 2-bit or 4-bit counter later (as PostgreSQL actually does) without changing the array type.

### `get(key)`

1. Lock the mutex.
2. Linear scan through `keys_` for a matching occupied slot. Returns `-1` on miss → throws `std::runtime_error`.
3. On hit: set `refbit_[slot] = 1`, return the value.

No hash map. For a buffer pool where N is in the hundreds, linear scan is predictable and has no hash collision edge cases. Fast enough.

### `put(key)`

1. Lock the mutex.
2. Scan for the key — if already present, refresh its ref bit and return.
3. If `size_ < capacity_`: call `claimFreeSlot()` to find the first empty slot.
4. If full: call `evict()` to run the clock sweep and get a free slot.
5. Write value, mark occupied, set ref bit.

### `evict()` — the clock sweep

```
limit = 2 × capacity   (prevents infinite loop)
repeat up to limit times:
    i = hand_
    advance hand_
    if slot i is empty:      return i   (free already)
    if refbit_[i] == 1:      refbit_[i] = 0   (second chance)
    if refbit_[i] == 0:      mark unoccupied, --size_, return i
if limit exhausted:          evict hand_ as fallback
```

The `2 × capacity` guard handles the worst case: every slot has `ref=1` on the first pass (all get cleared), then on the second pass they're all `ref=0` so eviction happens. The fallback after that is a safety net that should never trigger in normal use.

### Background thread

Runs every N seconds via `bgLoop()`. Acquires the mutex and sets `refbit_[i] = 0` for every occupied slot. This ages all entries simultaneously — the next `get()` on any of them will re-raise the bit; entries nobody touches will stay at 0 and be easy victims for the clock sweep.

Shutdown is signalled via a `std::atomic<bool> stop_` using `memory_order_release` on write and `memory_order_acquire` on read — the minimal ordering needed to guarantee the thread sees the flag before it loops again.

---

## Thread safety

A single `std::mutex mtx_` protects `keys_`, `occupied_`, `refbit_`, `size_`, and `hand_`. The class is explicitly non-copyable (deleted copy constructor and assignment) because owning a `std::thread` makes copying nonsensical.

---

## Generics and future `Page` support

`T` only needs `operator==` — no hashing required. To plug in a `Page` type:

```cpp
struct Page {
    int pageId;
    bool operator==(const Page& other) const {
        return pageId == other.pageId;
    }
};

ClockSweep<Page, 128> bufferPool(128);
```

That's it. The linear scan in `find()` uses `operator==` directly.

The trade-off vs. version 1: adding a hash would make `find()` O(1) instead of O(N), but for small N the scan is fine and the code stays simpler.

---

## Building

```bash
mkdir build && cd build
cmake ..
make
./db_engine
```

Requires a C++17 compiler and pthreads (`find_package(Threads)` in CMakeLists handles linkage).