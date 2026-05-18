# Lab 03 - Clock Sweep Cache Replacement Algorithm

## Objective

The objective of this lab is to implement the Clock Sweep cache replacement algorithm in C++ and understand how modern database systems and operating systems manage memory pages efficiently.

This lab includes:

- Integer-based Clock Sweep implementation
- Generic templated Clock Sweep implementation
- Background sweeper thread using `std::thread`
- Simulation of database page replacement
- Understanding of cache eviction policies

---

# What is Clock Sweep?

Clock Sweep is an approximation of the LRU (Least Recently Used) cache replacement algorithm.

Instead of maintaining an exact usage order of pages, Clock Sweep uses:

- a circular buffer
- a clock hand pointer
- reference bits

This approach is more scalable and efficient for large systems.

Clock Sweep is commonly used in:

- PostgreSQL Buffer Manager
- Operating Systems
- Database Buffer Pools
- Virtual Memory Systems

---

# Project Structure

```bash
Lab_03/
│
├── IntegerClockSweep.cpp
├── GenericClockSweep.cpp
├── README.md
├── analysis.md
└── screenshots/
```

---

# Features

## Integer Version

- Supports integer keys
- Implements:
  - `put()`
  - `get()`
- Simulates basic Clock Sweep replacement

---

## Generic Version

- Uses C++ templates
- Supports generic data types
- Simulates database pages
- Includes background sweeper thread
- Uses `std::thread`

---

# Clock Sweep Working

Each frame contains:

- key/page
- reference bit

When a page is accessed:

```text
refBit = 1
```

When eviction occurs:

- if `refBit == 1`
  - set to `0`
  - skip frame

- if `refBit == 0`
  - evict page
  - insert new page

This creates a circular sweeping behavior similar to a clock hand.

---

# Integer Version

## Compilation

```bash
g++ IntegerClockSweep.cpp -o integer_clock
```

## Execution

```bash
./integer_clock
```

---

# Generic Version

## Compilation

```bash
g++ GenericClockSweep.cpp -o generic_clock -pthread
```

## Execution

```bash
./generic_clock
```

---

# Why `-pthread`?

The generic implementation uses:

```cpp
std::thread
```

Therefore thread library linking is required.

---

# Background Sweeper Thread

The generic version includes a background thread that continuously runs every second.

This simulates:

- database background workers
- page cleaners
- eviction daemons
- PostgreSQL bgwriter process

---

# Concepts Covered

- Cache Replacement Algorithms
- Clock Sweep
- LRU Approximation
- Reference Bits
- Circular Buffer
- Generic Programming
- C++ Templates
- Multithreading
- Background Daemon Simulation
- Database Buffer Management

---

# Sample Output

```text
Buffer State:
[Page1 ref=1] [Page2 ref=1] [Page3 ref=1]

Background sweep running... hand=0

Buffer State:
[Page1 ref=1] [Page4 ref=1] [Page3 ref=0]
```

---

# Learning Outcomes

Through this lab, the following concepts were understood:

- How databases manage in-memory pages
- Why exact LRU is expensive
- Why Clock Sweep is scalable
- How background maintenance threads work
- How generic programming improves flexibility
- How operating systems and databases perform eviction

---

# Real World Connection

Real database systems such as PostgreSQL use Clock Sweep-like algorithms inside the Buffer Pool Manager to efficiently manage memory pages.

Operating systems also use similar page replacement strategies in virtual memory management.

---

# Author

Prince Kumar

Scaler School of Technology  
BITS Pilani

---

# Conclusion

This lab helped in understanding:

- memory management
- page replacement
- database internals
- multithreading
- scalable cache eviction strategies

The Clock Sweep algorithm provides a practical and efficient approximation to LRU while reducing overhead and improving scalability.