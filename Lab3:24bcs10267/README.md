# Lab 3 — Clock Sweep Buffer Cache

**Roll Number:** 24BCS10267
**Name:** Ujjawal Prabhat

---

## Overview

This lab implements the **Clock Sweep (Second-Chance)** page replacement
algorithm in modern C++ as a fixed-size, generic, thread-safe buffer cache.

Key properties:

- Generic over the stored key type (`template <typename K>`)
- Internally locked with `std::mutex` — safe to use from multiple threads
- A background "aging" thread periodically clears reference bits
- CLOCK sweep eviction with the standard second-chance rule
- Demo scenarios for both `int` and `std::string` caches

---

## Project Layout

```text
Lab3:24bcs10267/
├── CMakeLists.txt
├── main.cpp
└── README.md
```

---

## Build & Run

### One-shot compile

```bash
g++ -std=c++17 -pthread -Wall -Wextra -Wpedantic main.cpp -o clock_cache
./clock_cache
```

### With CMake

```bash
cmake -S . -B build
cmake --build build
./build/clock_cache
```

---

## Public API

| Method        | Description                                          |
|---------------|------------------------------------------------------|
| `put(key)`    | Inserts or refreshes a key in the cache              |
| `get(key)`    | Returns the stored key and sets its reference bit    |
| `contains(k)` | Checks whether a key is present without touching it (still locks) |
| `size()`      | Number of live entries currently held                |
| `dump(label)` | Pretty-prints frame state and cursor position        |

---

## Clock Sweep Algorithm

Each frame in the cache stores:

- the key
- a `live` flag (is the slot occupied?)
- a `used` reference bit

**On insertion into a full cache:**

1. Starting at the clock cursor, scan circularly.
2. If `used == 1`, give the frame a second chance: clear the bit and advance.
3. If `used == 0`, evict that frame and place the new key there.
4. Move the cursor one past the victim.

A background thread wakes on a fixed interval and clears every `used` bit,
so frames that haven't been touched recently age out and become eviction
candidates on the next sweep.

---

## Sample Output

```text
===== INTEGER CACHE =====

[Initial Fill] cursor=0 -> 10(ref=1) 20(ref=1) 30(ref=1) 40(ref=1)

[After Aging] cursor=0 -> 10(ref=0) 20(ref=0) 30(ref=0) 40(ref=0)

[Touched 20 and 40] cursor=0 -> 10(ref=0) 20(ref=1) 30(ref=0) 40(ref=1)

[Inserted 50] cursor=1 -> 50(ref=1) 20(ref=1) 30(ref=0) 40(ref=1)

[Inserted 60] cursor=3 -> 50(ref=1) 20(ref=0) 60(ref=1) 40(ref=1)

20 exists: 1
10 exists: 0
Current size: 4

===== STRING CACHE =====

[Initial] cursor=0 -> apple(ref=1) banana(ref=1) orange(ref=1)

[After inserting grape] cursor=1 -> grape(ref=1) banana(ref=0) orange(ref=0)

banana exists: 1
apple exists: 0

Program Finished
```

---

## Walking Through the Output

- All freshly inserted frames start with `ref = 1`.
- The aging worker resets every reference bit to `0` on its tick.
- `get()` re-arms the reference bit for the touched key.
- When the cache is full and a new key arrives, the sweep:
  - skips frames whose bit is still `1` (and clears them),
  - evicts the first frame it finds with `ref = 0`.

Example from the integer run: `10` was evicted because it was untouched
after aging, while `20` survived because it had been recently accessed.

---

## Technologies

- C++17
- STL: `vector`, `unordered_map`, `thread`, `mutex`, `condition_variable`
- Multithreading with periodic background work
