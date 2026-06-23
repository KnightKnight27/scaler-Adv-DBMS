# Lab 3 - Clock Sweep Buffer Pool Replacement Algorithm

**Name:** Abhijit P
**Roll No:** 24bcs10175

## Objective

The objective of this lab was to implement the Clock Sweep page replacement algorithm in C++ and understand how PostgreSQL performs page eviction inside its buffer pool.

The implementation simulates a fixed-size cache where pages are assigned reference bits and are replaced using the Clock Sweep strategy when the cache becomes full.

---

## Overview

Clock Sweep is a page replacement algorithm used in database systems to decide which page should be removed from memory when space is needed.

Instead of maintaining an exact usage order like LRU (Least Recently Used), Clock Sweep uses a reference bit to track recently accessed pages.

This provides similar behavior to LRU while requiring less overhead.

---

## PostgreSQL Buffer Pool

PostgreSQL stores frequently accessed disk pages inside a shared buffer pool.

When the buffer pool becomes full, PostgreSQL must select a page for eviction.

To perform this efficiently, PostgreSQL uses a Clock Sweep based replacement strategy.

The implementation in this lab simulates the same concept using cache entries and reference bits.

---

## Features

* Generic implementation using C++ templates
* Fixed-size cache
* Clock Sweep replacement policy
* Reference bit management
* Background thread using `std::thread`
* Thread-safe operations using `std::mutex`
* Demonstration of page insertion, access, and eviction

---

## Clock Sweep Algorithm

Each cache entry contains:

* Key
* Reference Bit

When a page is accessed:

```text
referenceBit = 1
```

When the cache becomes full:

1. The clock hand scans cache entries.
2. If the reference bit is `1`, the page receives a second chance and the bit is reset to `0`.
3. If the reference bit is `0`, the page is selected for eviction.
4. The new page is inserted in its place.

---

## Example

Initial Cache:

```text
[1] [2] [3]
```

Reference Bits:

```text
[1] [1] [1]
```

Access:

```text
getKey(1)
```

Insert:

```text
putKey(4)
```

Clock Sweep Operation:

```text
1 -> second chance
2 -> second chance
3 -> second chance
1 -> evicted
```

Final Cache:

```text
[4] [2] [3]
```

---

## Project Files

```text
Lab3/
├── README.md
├── main.cpp
└── CMakeLists.txt
```

### main.cpp

Contains the complete Clock Sweep implementation, cache management logic, and demonstration code.

### CMakeLists.txt

Build configuration file for compiling the project.

---

## Build Instructions

```bash
mkdir build
cd build
cmake ..
make
./db_engine
```

---

## Expected Output

The program demonstrates:

* Cache insertion
* Cache access
* Reference bit updates
* Clock Sweep traversal
* Page eviction
* Final cache state

Example:

```text
Inserted 1 into cache
Inserted 2 into cache
Inserted 3 into cache

Accessed key 1

Second chance given to 1
Second chance given to 2
Second chance given to 3

Evicting 1 -> inserting 4
```

---

## Conclusion

This lab demonstrated the implementation of the Clock Sweep page replacement algorithm in C++. The algorithm uses reference bits and a circular clock hand to provide an efficient approximation of LRU page replacement.

The implementation also illustrates the core idea behind PostgreSQL's buffer pool eviction strategy, where pages that have been accessed recently receive a second chance before being replaced.
