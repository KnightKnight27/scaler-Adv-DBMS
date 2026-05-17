# Clock Sweep Page Replacement Algorithm

## Overview

This project demonstrates the implementation of the **Clock Sweep Page Replacement Algorithm** in C++.

Clock Sweep is an efficient approximation of the **Least Recently Used (LRU)** page replacement strategy used in:

- Operating Systems
- Database Buffer Managers
- Virtual Memory Systems
- Cache Management Systems

Instead of maintaining exact usage order like LRU, Clock Sweep uses a **reference bit** and a **circular clock hand** to decide which page to evict.

---

# Features

- Fixed-size buffer/cache
- Page insertion and retrieval
- Second-chance mechanism using reference bits
- Automatic page eviction when buffer is full
- Circular clock-hand traversal
- Generic implementation using C++ templates
- Console visualization of cache state

---

# How Clock Sweep Works

Each frame in memory contains:

| Field | Description |
|---|---|
| Key | Page identifier |
| Value | Stored page data |
| Valid Bit | Indicates whether frame is occupied |
| Reference Bit | Indicates recent usage |

---

## Algorithm Steps

### 1. Page Access
When a page is accessed:
- Its reference bit becomes `true`

### 2. Buffer Full Condition
When inserting into a full buffer:
- The clock hand checks frames one by one

### 3. Second Chance
If a frame's reference bit is:
- `true` → clear it and move ahead
- `false` → evict the page

### 4. Replacement
The new page is inserted into the evicted frame.

---

# Data Structures Used

## 1. Vector
```cpp
vector<Frame> frames_;
```

Stores all cache frames.

---

## 2. Hash Map
```cpp
unordered_map<Key, size_t> index_map_;
```

Provides O(1) lookup for pages.

---

## 3. Circular Pointer
```cpp
size_t clock_hand_;
```

Acts as the rotating clock hand.

---

# Time Complexity

| Operation | Complexity |
|---|---|
| Get Page | O(1) |
| Insert Page | O(1) Average |
| Eviction | O(n) Worst Case |

---

# Sample Execution

## Initial Inserts
```text
Insert page 1 into free frame 0
Insert page 2 into free frame 1
Insert page 3 into free frame 2
```

---

## Access Pages
```text
Page hit: 1 found in frame 0
Page hit: 2 found in frame 1
```

---

## Buffer Full Eviction
```text
Buffer full. Searching victim for page 4

Frame 0 has reference bit = true
Giving second chance

Frame 1 has reference bit = true
Giving second chance

Evicting page 3 from frame 2
Insert page 4 into frame 2
```

---

# Compilation & Execution

## Compile
```bash
g++ main.cpp -o clock
```

## Run
```bash
./clock
```

---

# Concepts Demonstrated

- Page Replacement Algorithms
- Cache Management
- Memory Management
- Circular Queue Traversal
- Template Programming in C++
- Hash-Based Lookup
- Buffer Pool Design

---

# Applications

Clock Sweep is widely used in:

- Linux memory management
- Database systems like PostgreSQL
- Buffer pool managers
- CPU cache simulations
- Virtual memory systems

---

# Future Improvements

Possible enhancements:
- Multi-threaded support
- Dirty bit handling
- Statistics tracking (hits/misses)
- Variable-sized pages
- Persistent storage integration
- GUI visualization

---

# Author

Developed as part of Operating Systems / System Design learning and demonstration project.