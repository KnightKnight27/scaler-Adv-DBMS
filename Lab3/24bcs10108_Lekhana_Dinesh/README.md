# Lab 3 - Clock Sweep Algorithm

Name: Lekhana Dinesh

Roll No: 24BCS10108

## Objective
Implement the Clock Sweep page replacement algorithm in C++ using a fixed-size buffer and a circular clock hand. The implementation should demonstrate page insertion, page hits, page misses, second chances, and eviction.

## Explanation
Clock Sweep is a page replacement policy that maintains a circular list of buffer frames and a reference bit for each page. When the buffer is full, the algorithm inspects frames in a circular fashion. If a frame has its reference bit set, it is given a second chance and the bit is cleared. If the reference bit is not set, that frame is evicted and the new page is inserted.

## Files Included
- `clock_sweep.cpp`
- `README.md`

## Features
- Generic template class `ClockSweep<Key, Value>`.
- Fixed capacity buffer with page frames.
- O(1) lookup using `unordered_map`.
- `put(key, value)`, `get(key)`, `contains(key)`, and `display()` methods.
- Page hit and miss reporting.
- Second chance handling on eviction.
- Simple demo in `main()`.

## Build and Run

### Linux / macOS
```bash
g++ -std=c++17 -Wall -Wextra -pedantic Lab3/24bcs10108_Lekhana_Dinesh/clock_sweep.cpp -o clock_sweep
./clock_sweep
```

### Windows PowerShell
```powershell
g++ -std=c++17 -Wall -Wextra -pedantic Lab3\24bcs10108_Lekhana_Dinesh\clock_sweep.cpp -o clock_sweep.exe
.\clock_sweep.exe
```

## Expected Behavior
- The demo creates a `ClockSweep<int, string>` cache with capacity 3.
- Pages are inserted until the buffer is full.
- Accessing existing pages sets their reference bits and shows page hits.
- Inserting a new page when full triggers the clock sweep eviction mechanism.
- The output shows second chances, eviction decisions, and the final buffer state.

## Time Complexity
- `get(key)`: O(1) due to hash map lookup.
- `put(key, value)` when updating or inserting into a free frame: O(1).
- `put(key, value)` when eviction is required: O(n) worst-case, but the clock hand may reuse cleared frames so it behaves like O(1) amortized in practical buffer workloads.
