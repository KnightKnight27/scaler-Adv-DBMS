# Lab 3 — Clock Sweep Buffer Cache (C++)

**Roll No.** 24BCS10406
**Name.** Manasvi Sabbarwal

## Objective

Implement the **clock sweep** (second-chance) page-replacement algorithm in
C++ as a generic, thread-safe buffer cache suitable for a database
storage-buffer manager.

### Requirements

- Templated (`template<typename T>`) so it generalizes to any data type.
- Fixed-capacity buffer with `getKey` / `putKey` operations.
- Background sweeper thread for periodic reference-bit aging.
- Thread-safe access from both foreground callers and the background sweeper.

## Algorithm Overview

The clock sweep algorithm treats the buffer as a **circular array** with a
**clock hand** pointer. Each frame has a **reference bit** that tracks
recent usage.

### Core operations

| Operation | Behaviour |
|---|---|
| `getKey(key)` | Cache hit → set `refBit = 1`, return key. Miss → return `T{}`. |
| `putKey(key)` — key exists | Refresh: set `refBit = 1`, return. |
| `putKey(key)` — free frame | Place entry in first available frame, advance hand. |
| `putKey(key)` — buffer full | Run clock sweep from current hand position: if `refBit == 0` → evict (victim found); if `refBit == 1` → clear to 0 (second chance), advance. |
| Background sweep | Periodically clears all reference bits to 0 (aging step). |

The periodic aging ensures that entries not accessed between sweeps become
eviction candidates. This mirrors the approach used in production systems
like PostgreSQL's buffer manager.

## Files

| File | Description |
|---|---|
| `main.cpp` | `ClockSweep<T>` class + `main()` with three demo scenarios |
| `CMakeLists.txt` | CMake build config (C++17, pthreads, warnings enabled) |

## Public API

```cpp
template <typename T>
class ClockSweep {
public:
    explicit ClockSweep(std::size_t capacity,
                        std::chrono::milliseconds sweepIntervalMs = 500ms);
    ~ClockSweep();                          // joins background thread

    T    getKey(const T& key);              // hit → key + sets ref bit
                                            // miss → T{}
    void putKey(const T& key);              // insert/refresh; evicts when full
    bool contains(const T& key);            // query without side effects
    std::size_t size();
    std::size_t capacity() const;

    void dump(const std::string& tag);      // debug: print buffer state
};
```

## Thread Safety

- A single `std::mutex` protects all shared state (buffer frames, lookup map,
  clock hand).
- The background sweeper acquires the lock only during the aging pass, then
  releases it and waits on a `std::condition_variable`.
- Clean shutdown: the destructor sets a flag, notifies the condition variable,
  and joins the sweeper thread — no busy-waiting.
- The class is non-copyable and non-movable.

## Build Instructions

### Using CMake

```bash
cmake .
make
./clock_sweep_cache
```

### Direct compilation

```bash
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread main.cpp -o clock_sweep_cache
./clock_sweep_cache
```

## Sample Output

```
=== Demo 1: Integer cache, capacity=4, sweep=300ms ===
[put 10] hand=1 | f0=10(r=1) f1=empty f2=empty f3=empty
[put 20] hand=2 | f0=10(r=1) f1=20(r=1) f2=empty f3=empty
[put 30] hand=3 | f0=10(r=1) f1=20(r=1) f2=30(r=1) f3=empty
[put 40 — full] hand=0 | f0=10(r=1) f1=20(r=1) f2=30(r=1) f3=40(r=1)

-- waiting 400ms for background sweep to age ref bits --
[after sweep] hand=0 | f0=10(r=0) f1=20(r=0) f2=30(r=0) f3=40(r=0)
[get 20 — ref set] hand=0 | f0=10(r=0) f1=20(r=1) f2=30(r=0) f3=40(r=0)
[get 40 — ref set] hand=0 | f0=10(r=0) f1=20(r=1) f2=30(r=0) f3=40(r=1)

-- inserting 50: should evict first frame with ref=0 --
[put 50] hand=1 | f0=50(r=1) f1=20(r=1) f2=30(r=0) f3=40(r=1)

-- inserting 60: next eviction via clock sweep --
[put 60] hand=3 | f0=50(r=1) f1=20(r=0) f2=60(r=1) f3=40(r=1)

contains(20)=true  contains(10)=false  contains(50)=true
size=4 / capacity=4

=== Demo 2: String cache, capacity=3, sweep=500ms ===
[filled] hand=0 | f0=red(r=1) f1=green(r=1) f2=blue(r=1)
[get red — ref set] hand=0 | f0=red(r=1) f1=green(r=1) f2=blue(r=1)
[after sweep — only red was re-touched] hand=0 | f0=red(r=0) f1=green(r=0) f2=blue(r=0)
[get red again — only red has ref=1] hand=0 | f0=red(r=1) f1=green(r=0) f2=blue(r=0)
[put yellow — evicts first ref=0 frame] hand=2 | f0=red(r=0) f1=yellow(r=1) f2=blue(r=0)
[put purple] hand=0 | f0=red(r=0) f1=yellow(r=1) f2=purple(r=1)
contains(red)=true  contains(green)=false  contains(blue)=false

=== Demo 3: Duplicate putKey refreshes ref bit, no growth ===
[filled] hand=0 | f0=100(r=1) f1=200(r=1) f2=300(r=1)
[after sweep — all aged] hand=0 | f0=100(r=0) f1=200(r=0) f2=300(r=0)
[put 200 again — ref refreshed] hand=0 | f0=100(r=0) f1=200(r=1) f2=300(r=0)
size=3 (unchanged)

All demos completed.
```

## Walkthrough — Demo 1

1. **Fill**: Insert 10, 20, 30, 40 → buffer `[10(1), 20(1), 30(1), 40(1)]`, hand=0.
2. **Background sweep** fires after ~300ms → all ref bits cleared to 0.
3. **Touch 20 and 40** via `getKey` → `[10(0), 20(1), 30(0), 40(1)]`.
4. **Insert 50** — buffer full, clock sweep from hand=0:
   - Frame 0: ref=0 → victim. Evict 10, insert 50.
5. **Insert 60** — clock sweep from hand=1:
   - Frame 1: ref=1 → second chance, clear to 0.
   - Frame 2: ref=0 → victim. Evict 30, insert 60.

Result: recently-used entries (20, 40) survived; cold entries (10, 30) were evicted — textbook second-chance behaviour.

## Extending to Page Objects

Since `ClockSweep<T>` is templated, it can be instantiated with a `Page` type
once the storage layer is built. The only constraint on `T` is that it must be
hashable (for `std::unordered_map`) and default-constructible. For a real
key-value cache (`PageId → Page`), the `Frame` struct can be extended to hold
a separate key and value field.
