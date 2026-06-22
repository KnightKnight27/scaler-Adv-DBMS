# Clock Sweep Buffer Pool Replacement

A C++ implementation of the **clock sweep (second-chance) page replacement algorithm** as used in PostgreSQL's buffer manager.

## What It Does

1. **Simulates** a fixed-size buffer pool (default 4 slots)
2. **Processes** a sequence of page requests, showing hits and misses
3. **Traces** the clock hand as it sweeps through the pool looking for eviction candidates

## Quick Start

```bash
# Build
make

# Run
make run
```

Or manually:

```bash
g++ -std=c++17 -O2 -o clock_sweep clock_sweep.cpp
./clock_sweep
```

## How Clock Sweep Works

| Step | Condition | Action |
|------|-----------|--------|
| 1 | Page already in pool | Hit -- increment usage count |
| 2 | Page not in pool, empty slot exists | Load into empty slot |
| 3 | Page not in pool, no empty slots | Sweep the clock to find a victim |
| 3a | Current slot has usage > 0 | Decrement usage, advance hand (second chance) |
| 3b | Current slot has usage == 0 | Evict this page, load new one |

## Documentation

See [Assignment.md](Assignment.md) for the full writeup, including how this maps to PostgreSQL internals and comparison with LRU.

Praveen Kumar
24bcs10048

## Requirements

- g++ with C++17 support
