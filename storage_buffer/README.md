# Lab 3 — Clock Sweep Buffer Cache

**Roll No.:** 24BCS10318
**Name:** Utkarsh Raj

## What it is

The **clock sweep** (second-chance) algorithm is a page-replacement policy used
in database buffer managers and operating systems. It approximates LRU without
the cost of sorting or timestamp bookkeeping.

The idea is simple: every frame in the buffer pool carries a single **reference
bit**. When a page is accessed the bit is set to 1. When the cache is full and
a victim must be chosen, a clock hand sweeps the frames in order:

- **ref = 1** → clear the bit (give it a second chance) and move on.
- **ref = 0** → evict this frame.

If every frame had ref = 1 the hand makes a second pass; by then all bits have
been cleared and the first non-pinned frame is taken. A background thread runs
the same sweep on a timer, ageing cold pages before the foreground ever needs
to evict.

### Key design points

| Concern | Choice |
|---|---|
| Genericity | `template<typename T>` — works for `int`, `std::string`, or a real `Page` struct |
| Miss signal | `std::optional<T>` — `nullopt` means miss; the caller loads from disk then calls `putKey` |
| Thread safety | Single `std::mutex` guards all shared state; `std::condition_variable` wakes the sweeper |
| Pinning | `pin(key)` / `unpin(key)` — pinned frames are skipped by eviction (needed for in-flight writes) |
| Stats | `getStats()` returns hit/miss/eviction/sweep counts |

---

## File layout

```
storage_buffer/
├── main.cpp          # ClockSweep<T> implementation + demo tests
├── CMakeLists.txt    # build config
└── README.md
```

---

## Build and run

### Requirements

- CMake ≥ 3.10
- A C++17 compiler (GCC 8+, Clang 7+)
- pthreads (standard on Linux/macOS)

### Steps

```bash
cd storage_buffer
mkdir build && cd build
cmake ..
make
./db_engine
```

That's it. The executable runs all five self-tests and exits cleanly.

---

## Output after run

```
=== Clock Sweep Cache Demo ===

-- Test 1: hit / miss / eviction (capacity=3) --
getKey(10): 10
getKey(99): MISS
ClockSweep  [hand=1  3/3]
------------------------------------------------------
 Frame     Value  Occupied       Ref    Pinned
     0        40         1         1         0
     1        20         1         0         0  <-- hand
     2        30         1         0         0
hits=1 misses=1 evictions=1

-- Test 2: second-chance (capacity=3) --
page 2 cached: YES
page 3 cached: YES
page 4 cached: YES
ClockSweep  [hand=1  3/3]
------------------------------------------------------
 Frame     Value  Occupied       Ref    Pinned
     0         4         1         1         0
     1         2         1         1         0  <-- hand
     2         3         1         1         0

-- Test 3: pin / unpin (capacity=2) --
Caught: ClockSweep: all frames are pinned
getKey(200): HIT
ClockSweep  [hand=1  2/2]
------------------------------------------------------
 Frame     Value  Occupied       Ref    Pinned
     0       300         1         1         0
     1       200         1         1         1  <-- hand

-- Test 4: concurrent stress (8 frames, 4 threads, sweeper=50ms) --
hits=797 misses=3 evictions=777 sweeps=0   ← varies each run

-- Test 5: ClockSweep<std::string> (capacity=3) --
alpha cached: NO
ClockSweep  [hand=1  3/3]
------------------------------------------------------
 Frame     Value  Occupied       Ref    Pinned
     0     delta         1         1         0
     1      beta         1         0         0  <-- hand
     2     gamma         1         0         0

=== All tests passed ===
```

---

## Walking through the demo

### Test 1 — hit, miss, eviction

```
putKey(10) → frame 0, ref=1
putKey(20) → frame 1, ref=1
putKey(30) → frame 2, ref=1        cache full

getKey(10) → HIT,  ref bit stays 1
getKey(99) → MISS, returns nullopt

putKey(40) → buffer full, findVictim() runs:
  pass 1: frame 0 ref=1 → clear to 0, move on
           frame 1 ref=1 → clear to 0, move on
           frame 2 ref=1 → clear to 0, move on
  pass 2: frame 0 ref=0 → EVICT (page 10)
  install 40 in frame 0
```

After this the hand sits at frame 1. Pages 20 and 30 survive with ref=0.

### Test 2 — second chance protects recently-used pages

Pages 1, 2, 3 are inserted (all ref=1). Pages 2 and 3 are then re-accessed.
When page 4 is inserted:

- Pass 1 clears page 1's bit (never re-accessed) and moves on; pages 2 and 3
  also get cleared, but the hand reaches page 1 first.
- Pass 2 picks page 1 as victim (ref=0, first un-pinned frame).

Pages 2 and 3 survive; page 1 is gone.

### Test 3 — pinning

Both frames are pinned. Trying to insert a third key throws
`std::runtime_error`. After unpinning frame 0, the insert succeeds and frame 0
is evicted. Frame 1 (still pinned) is untouched.

### Test 4 — concurrency

Four threads each do 200 `putKey` + `getKey` pairs against an 8-frame cache
while the background sweeper fires every 50 ms. No locks are held across the
sleep, so foreground threads run freely between sweeps. Run with
`-fsanitize=thread` to verify no data races.

### Test 5 — template with `std::string`

Identical algorithm, different type. No code changes needed.

---
