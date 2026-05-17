# Clock Sweep Buffer Replacement Policy

## Overview

This project implements a thread-safe Clock Sweep cache replacement algorithm inspired by the buffer management system used in databases like PostgreSQL.

The Clock Sweep algorithm is an efficient approximation of the Least Recently Used (LRU) page replacement strategy and is commonly used in database systems because it scales better under concurrent workloads.

This implementation supports:

- O(1) average lookup
- Clock Sweep eviction
- Concurrent access using thread synchronization
- Background aging thread
- Generic template-based cache keys

---

# Project Structure

```text
24bcs10349_Namami_Verma/
├── CMakeLists.txt
├── README.md
├── include
│   └── ClockSweep.hpp
└── src
    └── main.cpp
```

---

# Clock Sweep Algorithm

The Clock Sweep algorithm organizes cache pages in a circular buffer.

Each page/frame contains a reference bit:

- `referenceBit = true`
  → page was recently used

- `referenceBit = false`
  → page can be evicted

A clock hand continuously moves circularly over the buffer frames.

---

# Eviction Logic

When cache becomes full:

## Case 1: Reference Bit = true

The page gets a second chance.

```text
referenceBit = false
move clock hand ahead
```

## Case 2: Reference Bit = false

The page is selected as the victim and evicted.

---

# Why Clock Sweep Instead of LRU?

Traditional LRU requires maintaining an ordered linked list of pages, which becomes expensive in highly concurrent systems.

Clock Sweep is preferred because:

- lower lock contention
- no linked list maintenance
- scalable under concurrency
- simpler memory management
- widely used in database engines

---

# Internal Design

The implementation uses:

| Component | Purpose |
|---|---|
| `std::vector` | Circular buffer storage |
| `std::unordered_map` | O(1) key lookup |
| `std::shared_mutex` | Concurrent read access |
| `std::mutex` | Frame-level synchronization |
| `std::thread` | Background aging thread |

---
# Frame Structure

Each cache frame stores:

```cpp
struct Frame {

    T key;

    bool occupied;

    bool referenceBit;

    std::mutex frameMutex;
};
```

---

# Thread Safety

This implementation is thread-safe.

## Synchronization Used

### 1. Shared Mutex

Used for protecting the page table:

```cpp
std::shared_mutex tableMutex;
```

Allows:

- multiple concurrent readers
- single writer

---

### 2. Frame-Level Mutex

Each frame contains its own mutex:

```cpp
std::mutex frameMutex;
```

This prevents race conditions while updating:

- reference bits
- occupancy state
- page replacement

---
# Background Aging Thread

A background thread periodically resets reference bits.

Purpose:

- simulate page aging
- allow inactive pages to become eviction candidates

The thread runs every second and clears recently used flags.

---

# Complexity Analysis

| Operation | Complexity |
|---|---|
| getKey() | O(1) |
| putKey() | O(1) average |
| eviction | O(N) worst case |

---

# Build Instructions

## Step 1: Create Build Directory

```bash
mkdir build
cd build
```

---

## Step 2: Generate Build Files

```bash
cmake ..
```

---

## Step 3: Compile

```bash
make
```
---
# Run Instructions

Execute:

```bash
./db_engine
```

---

# Example Workflow

```cpp
ClockSweep<int> clockSweep(4);

clockSweep.putKey(1);
clockSweep.putKey(2);
clockSweep.putKey(3);
clockSweep.putKey(4);

clockSweep.getKey(1);

clockSweep.putKey(5);
```

If the cache becomes full:

- recently accessed pages receive second chances
- inactive pages are evicted

---

# Key Features

- Thread-safe implementation
- Generic template-based cache
- Circular clock hand traversal
- Second-chance eviction policy
- Background aging mechanism
- Efficient concurrent access

---
# Learning Outcomes

This project demonstrates concepts related to:

- database buffer management
- cache replacement policies
- concurrent programming
- thread synchronization
- lock-based concurrency control
- system-level C++ design

---

# References

- PostgreSQL Buffer Manager
- Clock Sweep Page Replacement Algorithm
- Operating System Cache Replacement Policies
- Database System Buffer Pool Management