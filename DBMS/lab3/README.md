# Lab Session 3 – Clock Sweep Page Replacement Algorithm

**Name:** Amitabh Panda
**Roll No:** 24BCS10104

---

## Aim

The goal of this lab was to implement the Clock Sweep page replacement algorithm in C++ and understand how PostgreSQL manages page eviction inside its buffer pool.

The experiment demonstrates how frequently used pages remain in memory longer than rarely used pages while avoiding the overhead of maintaining a complete Least Recently Used (LRU) list.

---

# Introduction

Database systems keep recently accessed disk pages in a memory area called the **buffer pool**. When the buffer pool becomes full, the system must decide which page should be removed to make space for a new one.

PostgreSQL uses the **Clock Sweep** algorithm instead of a traditional LRU implementation.

The algorithm maintains:

* A circular list of buffer frames
* A clock hand that moves through the frames
* A usage counter for every page

When a page is accessed, its usage counter increases.

During replacement:

1. The clock hand examines a frame.
2. If the frame has a usage count greater than zero, the count is decreased and the frame receives another chance.
3. If the usage count becomes zero, that frame can be selected for eviction.
4. The hand continues moving in a circular fashion until a victim frame is found.

---

# Working Principle

```text
Page Request
      ↓
Page Found?
   /      \
 Yes      No
  ↓        ↓
Increase   Run Clock Sweep
Usage      Algorithm
Count         ↓
  ↓       Select Victim
 Continue     ↓
          Replace Page
```

The algorithm approximates LRU behavior while requiring much less maintenance.

---

# Program Implementation

The C++ program consists of:

### BufferFrame Structure

Each frame stores:

* Page ID
* Usage Count
* Pin Status

### ClockBufferPool Class

Responsible for:

* Managing frames
* Tracking loaded pages
* Selecting victim pages
* Simulating Clock Sweep replacement

### Main Function

A sequence of page requests is executed:

```text
1 2 3 4 1 2 5 1 2 3 4 5
```

This pattern generates both page hits and page misses, allowing observation of the replacement process.

---

# Sample Execution

```text
MISS -> Loaded Page 1 into Frame 0
MISS -> Loaded Page 2 into Frame 1
MISS -> Loaded Page 3 into Frame 2
MISS -> Loaded Page 4 into Frame 3

HIT -> Page 1 found in Frame 0
HIT -> Page 2 found in Frame 1

EVICT -> Removing Page 3 from Frame 2
MISS -> Loaded Page 5 into Frame 2
```

Pages that are accessed repeatedly accumulate larger usage counts and remain in memory longer.

---

# Why PostgreSQL Uses Clock Sweep

| Feature          | LRU         | Clock Sweep     |
| ---------------- | ----------- | --------------- |
| Recency Tracking | Exact       | Approximate     |
| Data Structure   | Linked List | Circular Buffer |
| Memory Overhead  | Higher      | Lower           |
| Maintenance Cost | High        | Low             |
| Scalability      | Moderate    | Better          |
| PostgreSQL Usage | No          | Yes             |

The Clock Sweep algorithm provides a practical balance between performance and implementation complexity.

---

# Advantages

* Simple implementation
* Low memory overhead
* Efficient for large buffer pools
* Reduced synchronization overhead
* Good approximation of LRU behavior

---

# Limitations

* Does not maintain exact page access order
* Victim selection may require multiple sweeps
* Approximate rather than perfect recency tracking

---

# Compilation and Execution

Compile the program:

```bash
g++ -std=c++17 clocksweep.cpp -o clocksweep
```

Run the executable:

```bash
./clocksweep
```

---

# Observations

* Frequently referenced pages received higher usage counts.
* Pages with low usage counts were selected for eviction.
* The clock hand continuously moved through the buffer frames in a circular manner.
* The algorithm successfully approximated LRU behavior without maintaining a sorted access list.

---

# Conclusion

This lab provided practical experience with PostgreSQL's Clock Sweep page replacement strategy. The algorithm uses a circular scanning mechanism and usage counters to identify replacement candidates efficiently.

Compared to traditional LRU, Clock Sweep achieves similar behavior while reducing computational overhead, making it well suited for high-performance database systems such as PostgreSQL.
