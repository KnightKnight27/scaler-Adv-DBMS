# Lab 3 — Clock Sweep Buffer Cache Implementation

## Name
Neerasa VedaVarshit

## Roll Number
24BCS10005

---

# Objective

To implement the Clock Sweep (Second-Chance) page replacement algorithm in C++ using a thread-safe fixed-capacity buffer cache.

---

# Introduction

The Clock Sweep algorithm is a cache/page replacement algorithm used in operating systems and database systems. It approximates the behavior of the Least Recently Used (LRU) algorithm while maintaining lower overhead.

The algorithm uses:

- Circular buffer
- Clock hand pointer
- Reference bits

Pages that were recently used receive a second chance before eviction.

---

# Features

- Generic templated implementation
- Fixed-size buffer
- Thread-safe cache access
- Background sweeper thread
- Automatic eviction using clock sweep
- Cache hit/miss handling
- Debug dump support

---

# Files

| File | Description |
|---|---|
| main.cpp | Clock Sweep implementation and test cases |
| CMakeLists.txt | CMake build configuration |
| README.md | Assignment documentation |

---

# Algorithm Overview

The cache is represented as a circular array.

Each frame contains:

- data
- reference bit
- valid bit

A clock hand continuously moves through the frames.

## Eviction Logic

When cache becomes full:

1. If reference bit = 1
   - clear it
   - give second chance
   - move clock hand

2. If reference bit = 0
   - evict frame
   - insert new page

---

# Public API

```cpp
T getKey(const T& key);

void putKey(const T& key);

bool contains(const T& key);

std::size_t size();

void dump(const std::string& tag);
```

---

# Thread Safety

The implementation uses:

- std::mutex
- std::condition_variable
- std::thread

The background thread periodically clears reference bits for aging.

---

# Build Instructions

## Using CMake

```bash
cmake .
make
./clock_sweep_cache
```

## Direct Compilation

```bash
g++ -std=c++17 -pthread main.cpp -o clock_sweep_cache

./clock_sweep_cache
```

---

# Sample Output

```text
=== Integer Cache Demo ===

[put 10] hand=1 | f0=10(r=1) f1=empty f2=empty f3=empty

[put 20] hand=2 | f0=10(r=1) f1=20(r=1) f2=empty f3=empty

[put 30] hand=3 | f0=10(r=1) f1=20(r=1) f2=30(r=1) f3=empty

[put 40] hand=0 | f0=10(r=1) f1=20(r=1) f2=30(r=1) f3=40(r=1)

[after sweep] hand=0 | f0=10(r=0) f1=20(r=0) f2=30(r=0) f3=40(r=0)

[put 50] hand=1 | f0=50(r=1) f1=20(r=1) f2=30(r=0) f3=40(r=1)
```

---

# Conclusion

The project successfully demonstrates:

- Clock Sweep replacement
- Second-chance eviction
- Concurrent-safe cache operations
- Efficient fixed-size memory management

The algorithm provides an efficient approximation to LRU with significantly lower implementation overhead.
