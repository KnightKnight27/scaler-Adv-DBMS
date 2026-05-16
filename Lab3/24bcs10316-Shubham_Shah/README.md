# Clock Sweep Algorithm

This lab implements the Clock Sweep page replacement algorithm in C++.

## Files

- `clock_sweep.cpp`: Templated `ClockSweep<T>` cache implementation and a runnable demo.

## How It Works

- Each frame stores one key and one reference bit.
- `getKey(key)` marks the matching frame as recently referenced.
- `putKey(key)` inserts a new key if space is available.
- When the cache is full, the clock hand scans frames:
  - If a frame has reference bit `1`, the algorithm clears it and gives the frame a second chance.
  - If a frame has reference bit `0`, that frame is replaced.

## Build And Run

From the repository root:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic Lab3/24bcs10316-Shubham_Shah/clock_sweep.cpp -o /tmp/clock_sweep
/tmp/clock_sweep
```
