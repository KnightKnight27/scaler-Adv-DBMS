# ClockSweep Lab — README

**Description:**
Small cache simulator implementing the Clock (second-chance) replacement algorithm. The program reads operations from `input.txt`, updates or queries the cache, and prints the cache state after each operation.

**File:** `ClockSweep.cpp`

**Input format (file: `input.txt`):
- First line: a single integer `size` — the capacity of the clock buffer.
- Subsequent lines: three integers per line: `t k v`
  - `t = 1` => put operation: insert key `k` with value `v` into the cache.
  - `t = 2` => get operation: query key `k` (the `v` field is ignored for get).

Example `input.txt`:
```
3
1 1 10
1 2 20
1 3 30
2 1 0
1 4 40
2 2 0
2 3 0
2 4 0
1 2 25
2 2 0
```

**Behavior details:**
- `put(k,v)` inserts or updates a key; if the key exists, its usage count is incremented.
- On eviction, the clock hand decrements usage counts until it finds a slot with `usageCount == 0` or an invalid slot.
- `get(k)` returns the stored value or `0` if the key is not present (the program prints a not-found message).

**Compile (from repo root):**
```bash
g++ -std=c++17 -O2 -o ClockSweep scaler-Adv-DBMS-LABS/LAB-3/ClockSweep.cpp
```

**Run (from repo root):**
```bash
cd scaler-Adv-DBMS-LABS/LAB-3
./ClockSweep
```

**Sample output (truncated):**
```
Cache State:
[1:10 UC:1] [2:20 UC:1] [3:30 UC:1]
Get: Key=1, Value=10
Cache State:
[1:10 UC:2] [2:20 UC:1] [3:30 UC:1]
...
File successfully modified and saved
```

**Notes:**
- The program uses `input.txt` in the same directory; modify it to test different sequences.
- `get()` returns `0` as the sentinel for "not found" because the cache stores integers; change the implementation if you need a different sentinel.

---

Created for LAB-3 — ClockSweep
