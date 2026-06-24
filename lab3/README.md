# Lab 3 — ClockSweep Buffer Pool Replacement Algorithm

## Overview

This lab implements PostgreSQL's **ClockSweep** buffer pool replacement algorithm in C++. ClockSweep is a variant of the Clock (Second-Chance) algorithm that approximates LRU behavior with O(1) eviction decisions — critical for high-concurrency database systems.

## How ClockSweep Works

```
Buffer Pool (circular array of frames):

        ┌───────┐
    ┌───│Frame 0│───┐
    │   └───────┘   │
┌───────┐       ┌───────┐
│Frame 3│       │Frame 1│    ◄── Clock hand sweeps clockwise
└───────┘       └───────┘
    │   ┌───────┐   │
    └───│Frame 2│───┘
        └───────┘

Eviction Algorithm:
1. Start at clock_hand position
2. Check current frame:
   - pin_count > 0?  → Skip (page in use by a backend)
   - usage_count > 0? → Decrement usage_count, advance hand (second chance!)
   - usage_count = 0 AND pin_count = 0? → EVICT THIS FRAME ✓
3. Advance clock hand
4. Repeat until victim found
```

## Why Not LRU?

| Feature | LRU | ClockSweep |
|---------|-----|------------|
| **Data structure** | Doubly-linked list | Circular array + integer counter |
| **Eviction cost** | O(1) but needs list manipulation | O(1) amortized scan |
| **Concurrency** | Needs list lock (contention!) | Only needs atomic counter decrement |
| **Cache pollution** | Vulnerable to sequential scans | More resistant (usage_count) |
| **Used by** | MySQL InnoDB (modified) | **PostgreSQL** |

PostgreSQL chose ClockSweep because:
- It avoids the linked-list contention that LRU suffers under high concurrency
- The `usage_count` provides a frequency-based signal (not just recency)
- It naturally handles "scan-resistant" behavior for sequential reads

## Buffer Frame Structure

Each frame in the buffer pool tracks:

```
BufferFrame {
    page:        Page         // the actual disk page data
    valid:       bool         // is this frame occupied?
    dirty:       bool         // has the page been modified?
    pin_count:   uint32       // number of active users (pinned = can't evict)
    usage_count: uint32       // access frequency counter (0-5)
}
```

## Building and Running

```bash
make        # compile
make run    # compile and run
make clean  # cleanup
```

## Test Scenarios

| Test | What It Demonstrates |
|------|---------------------|
| **Test 1**: Basic Operations | Fetch, unpin, cache hit, eviction |
| **Test 2**: Dirty Pages | Flushing dirty pages before eviction |
| **Test 3**: Pin Protection | All-pinned scenario (buffer exhaustion) |
| **Test 4**: Usage Count | Second-chance mechanism, hot page survival |
| **Test 5**: Workload Sim | Sequential scan vs Zipfian vs working-set hit rates |

## Files

| File | Description |
|------|-------------|
| `clock_sweep.h` | Complete ClockSweep buffer pool implementation |
| `main.cpp` | Driver program with 5 test scenarios |
| `Makefile` | Build targets |
