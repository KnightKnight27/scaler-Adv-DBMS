# Viraj Vaibhav Bhanage 24bcs10274

# Clock Sweep Buffer Cache

## Overview

This project implements the Clock Sweep (Second-Chance) page replacement algorithm in C++ using a fixed-size buffer cache.

The cache supports:
- Generic templated storage (`template<typename T>`)
- Thread-safe operations using mutexes
- Background aging thread for clearing reference bits
- CLOCK sweep eviction policy
- Integer and string cache demonstrations

---

## Project Structure

```text
.
├── CMakeLists.txt
├── main.cpp
└── README.md
```

---

## Build Instructions

### Direct Compilation

```bash
g++ -std=c++17 -pthread -Wall -Wextra -Wpedantic main.cpp -o clock_cache
```

### Run

```bash
./clock_cache
```

---

## Features

- Fixed-capacity cache
- Generic implementation using templates
- Background sweeper thread
- Reference-bit aging
- CLOCK / Second-Chance eviction
- Thread-safe cache access

---

## Cache Operations

| Function | Description |
|---|---|
| `insert()` | Inserts or refreshes an item |
| `fetch()` | Retrieves an item and sets reference bit |
| `exists()` | Checks whether item exists |
| `printState()` | Prints cache contents |

---

## CLOCK Sweep Algorithm

Each cache slot stores:
- value
- occupied flag
- reference bit

When the cache becomes full:
1. The clock hand scans circularly.
2. If `refBit = 1`, the page gets a second chance and its bit becomes `0`.
3. If `refBit = 0`, the page is evicted.

A background worker periodically clears reference bits to age older entries.

---

## Sample Output

```text
===== INTEGER CACHE =====

[Initial Fill] hand=0 -> 10(ref=1) 20(ref=1) 30(ref=1) 40(ref=1)

[After Aging] hand=0 -> 10(ref=0) 20(ref=0) 30(ref=0) 40(ref=0)

[Touched 20 and 40] hand=0 -> 10(ref=0) 20(ref=1) 30(ref=0) 40(ref=1)

[Inserted 50] hand=1 -> 50(ref=1) 20(ref=1) 30(ref=0) 40(ref=1)

[Inserted 60] hand=3 -> 50(ref=1) 20(ref=0) 60(ref=1) 40(ref=1)

20 exists: 1
10 exists: 0
Current size: 4

===== STRING CACHE =====

[Initial] hand=0 -> apple(ref=1) banana(ref=1) orange(ref=1)

[After inserting grape] hand=1 -> grape(ref=1) banana(ref=0) orange(ref=0)

banana exists: 1
apple exists: 0

Program Finished
```

---

## Output Explanation

- Initially all inserted pages have `ref=1`
- Background aging resets reference bits to `0`
- Accessing pages updates their reference bits
- During insertion into a full cache:
  - recently used pages survive
  - older pages get evicted

Example:
- `10` was evicted
- `20` survived because it was recently accessed

---

## Technologies Used

- C++17
- STL (`vector`, `unordered_map`, `thread`, `mutex`)
- Multithreading
- Condition Variables
