# Clock Sweep Cache — Lab 3

**Course:** Advance DBMS  
**Student:** Vimal Kumar Yadav  
**Roll No:** 24BCS10273

---

## What is Clock Sweep?

Clock Sweep is a page eviction algorithm used by **PostgreSQL's buffer manager**. When memory is full and a new page needs to be loaded from disk, it decides which existing page to throw out.

Think of it like a clock hand sweeping around a circle. Each page on the clock has a usage count. The hand sweeps around and:
- If a page's count is **> 0** → decrement it, move on (second chance)
- If a page's count is **0** → evict it, load the new page here

This is a practical approximation of LRU (Least Recently Used) — cheaper to run but nearly as smart.

---

## How This Implementation Works

```
put(key, value)
    │
    ├── key exists? → update value, bump frequency
    │
    └── cache full? → evictLeastFrequent() → insert
                      otherwise just insert


get(key)
    │
    └── found? → bump frequency (capped at 5), return value
        not found? → return nullopt


Background Thread (every 1 second)
    │
    ├── decay every entry's frequency by 1
    │
    └── frequency hit 0? → evict immediately
```

---

## File Structure

```
.
├── Data.h                  # struct holding value + frequency
├── BufferTag.h             # key type — identifies a disk page (like postgres)
├── Page.h                  # value type — represents a page loaded from disk
├── ClockSweepCache.h       # cache class declaration
├── ClockSweepCache.tpp     # cache method implementations (template)
├── main.cc                 # test driver
└── CMakeLists.txt          # build config
```

### `Data.h`
A simple struct that wraps the cached value alongside its usage frequency.

### `BufferTag.h`
The key used to identify a page — directly inspired by PostgreSQL's `BufferTag`. Four integers uniquely identify any page on disk:

| Field | Meaning |
|---|---|
| `spcNode` | Tablespace ID |
| `dbNode` | Database ID |
| `relNode` | Relation (table) ID |
| `blockNum` | Block number within the file |

Also includes a custom `std::hash` specialization so it works as an `unordered_map` key.

### `Page.h`
Represents a page loaded into the buffer pool. PostgreSQL uses 8KB pages — this uses 64 bytes for testing. Supports `read()`, `write()`, and `print()`.

### `ClockSweepCache.h` + `ClockSweepCache.tpp`
The main cache. It is a **generic (template) class** over both key and value types:

```cpp
ClockSweepCache<BufferTag, Page> cache(3);   // postgres-style
ClockSweepCache<int, int>        cache(10);  // simple int cache
ClockSweepCache<std::string, MyObj> cache(5); // any types
```

Key design decisions:

- **Frequency cap at 5** — same as PostgreSQL. Prevents a hot page from becoming impossible to evict.
- **Mutex on all map access** — background thread and main thread share the map safely.
- **`condition_variable`** — destructor wakes the background thread immediately instead of waiting out the sleep.
- **`evictLeastFrequent()`** — called by `put()` when cache is full. Scans for the entry with the lowest frequency and removes it.

---

## Differences from Classic Clock Sweep

| | Classic Clock Sweep | This Implementation |
|---|---|---|
| Flag | Boolean (0 or 1) | Counter (0 to 5) |
| Eviction trigger | Clock hand reaches 0 | Background thread decay + `put()` pressure |
| Hot page protection | One sweep | Multiple sweeps proportional to frequency |
| Inspired by | Textbooks | PostgreSQL |

---

## Thread Safety

All `get()`, `put()`, and `remove()` calls lock a `std::mutex` before touching the map. The background thread also holds the same lock during its sweep. This prevents data races between the caller and the background thread.

```
Main Thread          Background Thread
    │                       │
    ▼                       ▼
lock(m_Mutex)          lock(m_Mutex)   ← one of these waits
access m_Map           decay + evict
unlock                 unlock
```

---

## Build & Run

```bash
# with logging (prints eviction events)
g++ -std=c++17 -DLOGGING -o clock_sweep main.cc -lpthread
./clock_sweep

# without logging
g++ -std=c++17 -o clock_sweep main.cc -lpthread
./clock_sweep
```

Or with CMake:
```bash
mkdir build && cd build
cmake ..
make
./ClockSweep
```

---

## Sample Output

```
--- initial pages loaded ---
Page content: [row: id=3 name=charlie]     ← accessed 3x
Page content: [row: id=3 name=charlie]
Page content: [row: id=3 name=charlie]
Page content: [row: id=2 name=bob]         ← accessed 1x

--- waiting for background thread to evict cold pages ---
Background evicting key                    ← alice evicted (freq decayed to 0)
Background evicting key                    ← bob evicted

--- loading new page (table=30 block=0) ---
--- loading new page (table=30 block=1) ---

--- verifying hot page (table=20 block=0) still in cache ---
Page content: [row: id=3 name=charlie]     ← charlie survived ✅
```

---