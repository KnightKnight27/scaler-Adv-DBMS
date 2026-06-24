# Lab 3: Clock Sweep Buffer Replacement

**Name:** Shah Musharaf ul Islam
**College ID:** 24bcs10447
**OS:** Arch Linux | **Shell:** zsh

---

## What is Clock Sweep?

Clock Sweep is the buffer eviction policy used by PostgreSQL. It's basically an approximation of LRU but without the overhead of maintaining a sorted list.

Each frame has a reference bit. When a page is accessed, its ref bit is set to `true`. When the cache is full and a new page needs to come in, the clock hand sweeps through frames:
- ref bit is `true` → give it a second chance, set ref bit to `false`, move on
- ref bit is `false` → evict this frame

---

## Project Structure

```
24bcs10447_Shah_Musharaf/
├── CMakeLists.txt
├── .gitignore
├── README.md
├── include/
│   └── ClockSweep.hpp
└── src/
    └── main.cpp
```

---

## Implementation

The cache is a fixed-size circular buffer backed by a `std::vector`. A `std::unordered_map` handles O(1) key lookup.

A background thread runs every second and clears all ref bits — this simulates page aging so pages that haven't been accessed recently become eviction candidates even if they were accessed a while ago.

**Frame struct:**
```cpp
struct Frame {
    T key;
    bool occupied = false;
    bool refBit   = false;
};
```

**Eviction logic (`findVictim`):**
```cpp
int findVictim() {
    while (true) {
        Frame& f = frames[clockHand];
        if (!f.occupied || !f.refBit) {
            int v = clockHand;
            clockHand = (clockHand + 1) % capacity;
            return v;
        }
        f.refBit = false;
        clockHand = (clockHand + 1) % capacity;
    }
}
```

---

## Build & Run

```zsh
mkdir build && cd build
cmake ..
make
./db_engine
```

---

## Output

```
Cache State:
  [1 | ref=1]
  [2 | ref=1]
  [3 | ref=1]
  [4 | ref=1]

Inserting 5 (eviction expected):

Cache State:
  [5 | ref=1]
  [2 | ref=0]
  [3 | ref=0]
  [4 | ref=0]

Inserting 6 (eviction expected):

Cache State:
  [5 | ref=1]
  [6 | ref=1]
  [3 | ref=0]
  [4 | ref=0]
```

Page 1 was evicted when 5 was inserted — even though `getKey(1)` was called, the clock hand had already swept past it and cleared its ref bit. Page 2 was evicted next for the same reason.

---

## Notes

- Used a single `std::mutex` instead of per-frame mutexes — simpler and good enough for this scale
- Background thread clears ref bits every second to simulate aging
- Template-based so it works with any key type, not just `int`
