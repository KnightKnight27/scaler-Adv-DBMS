# Lab 3 — Clock Sweep Buffer Replacement: Code Notes

Line-by-line annotations for `ClockSweep.hpp`, `main.cpp`, and `CMakeLists.txt`. Headers, member roles, method logic, and build config — all explained here so the source stays comment-free.

---

## File layout

| File | Role |
|---|---|
| `storage_buffer/ClockSweep.hpp` | The template class — full implementation lives here (templates require it; see "Why everything in the header?" below) |
| `storage_buffer/main.cpp` | Demo program: fills a 4-slot pool, runs reads, triggers eviction |
| `storage_buffer/CMakeLists.txt` | Build description for CMake |

---

## Headers used in `ClockSweep.hpp`

| Header | What we use from it | Notes |
|---|---|---|
| `<vector>` | `std::vector<Frame>` — the dynamic slot array | Java analogue: `ArrayList<Frame>` |
| `<unordered_map>` | `std::unordered_map<K, size_t>` — O(1) hashtable for key → slot lookup | Java analogue: `HashMap<K, Integer>` |
| `<optional>` | `std::optional<V>` — return type for `get()`, distinguishes hit/miss without sentinel values | C++17. Java analogue: `Optional<V>` |
| `<cstdint>` | `uint8_t` — small unsigned int for usage counter (0–5 fits in 1 byte) | |
| `<cstddef>` | `size_t` — unsigned type for sizes/indexes | Canonical "an index into a container" |
| `<iostream>` | `std::cout` — for the `dump()` debug helper | |

`<string>` is only included in `main.cpp` because that's where we use `std::string` as the V type.

### `#pragma once`
First line of the header. Tells the preprocessor "include this file at most once per translation unit." Prevents duplicate-definition errors if two `.cpp` files (or two headers) both include `ClockSweep.hpp`. Older equivalent is `#ifndef CLOCK_SWEEP_HPP / #define / #endif` guards.

---

## Headers used in `main.cpp`

| Header | Why |
|---|---|
| `"ClockSweep.hpp"` | Our template class. Quotes (not angle brackets) tell the preprocessor "look in the current directory first" |
| `<string>` | We instantiate `ClockSweep<int, std::string>`, so `std::string` must be visible |

---

## `ClockSweep` class — public API

### Template parameters
```cpp
template <typename K, typename V>
class ClockSweep
```
Generic over key and value types. Real Postgres uses something more concrete (`BufferTag → BufferDesc`), but generics make it easy to swap types for a teaching version.

### Constructor
```cpp
explicit ClockSweep(size_t capacity, uint8_t max_usage = 5)
    : capacity_(capacity), max_usage_(max_usage),
      frames_(capacity), hand_(0) {}
```
- `explicit` — blocks implicit conversion. Forces you to write `ClockSweep<int,string> p(4);` not `= 4`.
- `max_usage = 5` — Postgres's tuned default. Caps how "hot" a page can be: a page with usage=5 needs 5 sweep passes before becoming evictable.
- The `: capacity_(capacity), ...` part is the **member initializer list**. Initializes members before the constructor body runs. Like Java field initialization but with explicit syntax.
- `frames_(capacity)` calls `std::vector`'s constructor with `capacity` default-constructed elements, so each slot starts with `valid=false`, `usage=0`.

### `get(key)` — cache lookup
```cpp
std::optional<V> get(const K& key)
```
- Returns `std::optional<V>` — either contains a value or is empty.
- `index_.find(key)` returns an iterator. `== index_.end()` means key not found (miss).
- On hit: bump usage, return the value (copied out of the slot).
- `const K&` — pass by const reference. No copy, no mutation. Same idea as Java passing object references, except in C++ you have to ask for it.

### `put(key, value)` — insert or update
```cpp
void put(const K& key, const V& value)
```
Three cases:
1. **Already cached** — overwrite value, bump usage, done. Common when a "dirty" page is being updated.
2. **Pool has space** (`index_.size() < capacity_`) — linear scan for any `!valid` slot, install there. A real implementation would maintain a free list (queue of invalid slot indexes) for O(1) lookup; the linear scan is fine for tiny capacities.
3. **Pool full** — call `sweep_for_victim()`, evict the returned slot, install new entry. Set `hand_ = (victim + 1) % capacity_` so the next sweep starts past the slot we just installed.

### `dump()` — debug helper
Prints the entire buffer state in compact form: `[hand=N] [key:usage] [key:usage] ...`. Lets us trace eviction by eye in `main.cpp`.

---

## Private state

```cpp
struct Frame {
    K key{};
    V value{};
    uint8_t usage{0};
    bool valid{false};
};

size_t capacity_;
uint8_t max_usage_;
std::vector<Frame> frames_;
std::unordered_map<K, size_t> index_;
size_t hand_;
```

| Member | Role |
|---|---|
| `Frame` | One slot's contents |
| `capacity_` | Total slots, fixed at construction |
| `max_usage_` | Counter ceiling — stops hot pages from becoming unevictable |
| `frames_` | The slot array |
| `index_` | Reverse lookup `key → slot index`, makes `get()` O(1) |
| `hand_` | Current clock hand position |

### Why the trailing underscore (`capacity_`)?
Common C++ convention to distinguish private members from local variables / parameters. Java uses `this.capacity` for the same purpose — C++ doesn't have an implicit `this.` so naming is how we keep them apart.

### Why `bool valid`?
A `Frame` created by `vector(capacity)` is default-initialized: `key={}`, `value={}`, `usage=0`. Without `valid`, "empty slot" and "slot holding key=0 with usage 0" look identical. The flag disambiguates.

### Why a separate `index_` map?
Without it, `get(key)` would need to linearly scan all frames to find the matching key — O(n) per call. With it, hit checks are O(1). The cost is extra memory + having to keep the map in sync (update on install, erase on evict).

---

## Private helpers

### `bump_usage(slot)`
```cpp
if (frames_[slot].usage < max_usage_) frames_[slot].usage++;
```
Caps the counter. Without the cap, a page accessed 1000 times would survive 1000 sweep passes — effectively pinned forever. Cap = 5 means at most 5 sweep passes before this page can be evicted.

### `install(slot, key, value)`
Stamps the slot with new contents (`usage=1`, `valid=true`) and updates `index_`. Used by all three branches of `put()`.

### `sweep_for_victim()` — the core algorithm
```cpp
while (true) {
    Frame& f = frames_[hand_];
    if (f.usage == 0) return hand_;
    f.usage--;
    hand_ = (hand_ + 1) % capacity_;
}
```
- `Frame& f` is a **reference**, not a copy. `f.usage--` modifies the actual frame in `frames_`. (A `Frame f` would copy and modifications would be lost.)
- `% capacity_` wraps the hand — that's what makes it circular.
- **Termination guarantee:** each iteration either returns or decrements a counter. The sum of all counters can only decrease, so within at most `max_usage × capacity` steps, some slot must hit zero. With cap=5 and capacity=4, worst case is 20 steps.

---

## `main.cpp` walkthrough

1. **Construct** `pool(4)` — 4 slots, max_usage defaults to 5.
2. **Fill** with pages 1–4. All slots installed with usage=1.
3. **Hot reads** — `get(1)×3` bumps slot 0 to usage=4. `get(2)×1` bumps slot 1 to usage=2. Pages 3, 4 stay at 1.
4. **`put(5)`** triggers a sweep starting at hand=0:
   - `0:[1,4]` → dec to 3, hand→1
   - `1:[2,2]` → dec to 1, hand→2
   - `2:[3,1]` → dec to 0, hand→3
   - `3:[4,1]` → dec to 0, hand→0
   - `0:[1,3]` → dec to 2, hand→1
   - `1:[2,1]` → dec to 0, hand→2
   - `2:[3,0]` → **EVICT page 3**, install page 5, hand → 3
5. **Verify** — `get(5)` is a hit; `get(3)` is a miss.

---

## `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.10)
project(db_engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(db_engine main.cpp)
```

| Line | What it does |
|---|---|
| `cmake_minimum_required` | Aborts if CMake is older than 3.10. Required by modern CMake. |
| `project(db_engine)` | Project name. |
| `CMAKE_CXX_STANDARD 17` | Compile as C++17 (needed for `std::optional`). |
| `CMAKE_CXX_STANDARD_REQUIRED ON` | Error if compiler can't do C++17 (vs silently falling back). |
| `CMAKE_EXPORT_COMPILE_COMMANDS ON` | Generates `build/compile_commands.json` — clangd/IDEs read this for IntelliSense, error squiggles, jump-to-definition. |
| `add_executable(db_engine main.cpp)` | Build target `db_engine` from `main.cpp`. CMake follows `#include`s to find `ClockSweep.hpp` — we don't need to list headers. |

---

## Build & run

```sh
cmake -S . -B build   # configure (once, or when CMakeLists changes)
cmake --build build   # compile
./build/db_engine     # run
```

Expected output:
```
After filling:     [hand=0] [1:1] [2:1] [3:1] [4:1] 
After hot reads:   [hand=0] [1:4] [2:2] [3:1] [4:1] 
After put(5):      [hand=3] [1:2] [2:0] [5:1] [4:0] 
get(5) -> page-E
get(3) -> MISS (evicted)
```

---

## Why everything in the header?

`ClockSweep` is a **template** class. Templates are compiled per-instantiation: the compiler stamps out separate machine code for `ClockSweep<int, std::string>` and `ClockSweep<int, MyPage>`, etc. Every `.cpp` that uses the template must therefore see the full implementation, not just the declaration.

If the class were **non-template** (hardcoded key/value types like Postgres's real buffer manager), we'd split it:
- `ClockSweep.hpp` — class declaration, method signatures only
- `ClockSweep.cpp` — method bodies

A third option exists for templates: split into `.hpp` (declaration) and `.tpp` (template implementation), with `#include "ClockSweep.tpp"` at the bottom of the `.hpp`. Pure stylistic — functionally identical to "everything in .hpp."

---

## What's missing from a real buffer pool

Things we intentionally skipped:
- **Pinning** — a real DB can't evict a page being actively read by a query. Each frame would have a `pin_count`; sweep skips frames with `pin_count > 0`.
- **Dirty pages** — a modified page must be flushed to disk before eviction. Frame needs a `dirty` flag + an "I/O when evicted" path.
- **Concurrency** — atomic `usage` counters, lock-free hashtable, per-frame locks for pinning.
- **Background sweeper** — Postgres has a background writer that opportunistically cleans dirty pages so foreground evictions are faster.
