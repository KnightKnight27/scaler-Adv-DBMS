# Analysis - Clock Sweep Cache Replacement Algorithm

## Introduction

Clock Sweep is a cache replacement algorithm used in operating systems and database systems to efficiently manage memory pages.

It is widely considered an optimized approximation of the Least Recently Used (LRU) algorithm.

The main goal of this lab was to understand:

- cache eviction
- page replacement
- memory management
- database buffer pool behavior
- background maintenance threads

---

# Why Cache Replacement is Needed

Memory is limited.

When the cache or buffer pool becomes full, the system must decide:

```text
Which page should be removed?
```

This process is called:

```text
Page Replacement
```

---

# LRU (Least Recently Used)

LRU removes the page that has not been used for the longest time.

Although accurate, LRU is expensive because it requires:

- maintaining ordering
- linked list updates
- synchronization
- lock management

In large systems this becomes costly.

---

# Why Clock Sweep?

Clock Sweep approximates LRU while being much more efficient.

Advantages:

- simpler implementation
- lower overhead
- scalable
- fewer lock operations
- better performance in concurrent systems

---

# Core Idea of Clock Sweep

Clock Sweep maintains:

- circular buffer
- clock hand pointer
- reference bit for each frame

Each frame contains:

```text
Page + Reference Bit
```

---

# Working of Clock Sweep

## Page Access

Whenever a page is accessed:

```text
refBit = 1
```

---

## During Eviction

The clock hand moves circularly.

### Case 1

If:

```text
refBit == 1
```

then:

- set refBit to 0
- skip page

This gives the page a second chance.

---

### Case 2

If:

```text
refBit == 0
```

then:

- evict page
- insert new page

---

# Why It Is Called Clock Sweep

The pointer continuously moves in a circular manner like the hand of a clock.

Example:

```text
[1] [2] [3] [4]
 ^
clock hand
```

---

# Integer Implementation

The first implementation used integers to simulate pages.

Functions implemented:

- `put()`
- `get()`
- `display()`

This helped in understanding:

- basic eviction logic
- reference bit updates
- clock hand movement

---

# Generic Implementation

The second implementation used:

```cpp
template<typename T>
```

This allowed the Clock Sweep class to work with:

- integers
- strings
- database pages
- custom objects

This improved flexibility and reusability.

---

# Simulating Database Pages

The generic version can model database pages.

Example:

```cpp
ClockSweep<Page> buffer(5);
```

This closely resembles real database buffer managers.

---

# Background Sweeper Thread

A background thread was implemented using:

```cpp
std::thread
```

The thread runs every second and simulates:

- background maintenance
- page scanning
- eviction daemon activity

---

# Why Background Threads are Important

Modern databases use background worker processes for:

- page cleaning
- checkpointing
- eviction
- WAL flushing
- memory management

Examples:

- PostgreSQL bgwriter
- Linux kswapd
- InnoDB page cleaner

---

# Thread Behavior

The background thread continuously executes:

```cpp
while(running)
```

and sleeps for one second between iterations.

This simulates periodic maintenance activity inside a database engine.

---

# Complexity Analysis

## put()

Worst case:

```text
O(n)
```

because the clock hand may scan multiple frames.

---

## get()

Worst case:

```text
O(n)
```

because the buffer is searched linearly.

---

# Advantages of Clock Sweep

- Efficient approximation of LRU
- Lower overhead
- Better scalability
- Simpler synchronization
- Suitable for databases
- Good cache performance

---

# Limitations

- Not perfectly accurate like true LRU
- Some frequently used pages may still get evicted
- Linear scan possible during replacement

---

# Real World Usage

Clock Sweep style algorithms are used in:

- PostgreSQL
- Linux memory management
- Database buffer pools
- Virtual memory systems
- Cache managers

---

# Key Learnings

This lab provided understanding of:

- cache replacement
- page management
- database internals
- memory eviction
- reference bits
- circular scanning
- generic programming
- multithreading

---

# Conclusion

The Clock Sweep algorithm provides an efficient and scalable solution for cache replacement.

Compared to exact LRU, it significantly reduces maintenance overhead while still maintaining good cache behavior.

The addition of generic programming and background threading helped simulate real database system behavior and provided insight into how modern systems manage memory pages efficiently.