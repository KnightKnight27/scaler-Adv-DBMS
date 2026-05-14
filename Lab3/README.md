# Lab 3 - Clock Sweep Cache Algorithm

## Student Info
- **ID:** 24BCS10031
- **Name:** Prabal Patra

---

## Overview

Implements Clock Sweep page replacement algorithm in C++ with:
- Template-based generic cache
- Background clock hand thread
- Thread-safe operations
- Extensible for complex types (DB pages)

## Key Components

### ClockSweep<T>
- `get(key)` - access item, set use bit
- `put(key)` - insert/update item, evict if full
- Thread-safe via mutex

### Algorithm
- Circular buffer with use bits
- Background thread advances clock hand
- Entries with use=0 are evicted first

## Build & Run

```bash
g++ -std=c++17 -pthread clock_sweep.cpp -o clock_sweep
./clock_sweep
```

## Files

- `clock_sweep.cpp` - main implementation
- `clock_sweep_test.cpp` - unit tests
- `README.md` - this file