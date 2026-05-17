# Lab 3 — Clock Sweep

**Name:** Parth Sankhla
**Roll Number:** 24BCS10229

A templated `ClockSweep<T>` cache that uses the clock (second-chance) algorithm for eviction. The sweep runs on its own background thread (`evictionClockThread`) and is woken up by `put()` whenever the cache is full.

## What's inside

- `main.cpp` — the `ClockSweep<T>` class plus a small demo with `int` and `string` caches.
- `CMakeLists.txt` — builds against C++17 and links pthreads.

## Build & run

```bash
cmake -S . -B build
cmake --build build
./build/clock_sweep
```

## How it works

Each frame has a `ref` bit. `put` sets it on insert, `get` sets it on access. When the cache is full, `put` signals the eviction thread, which walks the clock hand: if a frame's `ref` is 1 it gets cleared (second chance), if it's 0 the frame is evicted. The hand wraps around the buffer.

Designed to be generic — `T` can be swapped for a page type later when this gets wired into the buffer manager.
