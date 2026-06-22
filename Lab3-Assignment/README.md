# Lab Session 3 — Solution: ClockSweep Page-Replacement Algorithm (C++)

My completed solution to **`lab_sessions/lab_3.txt`** — an implementation of the
**ClockSweep** buffer-pool eviction algorithm that PostgreSQL uses in its shared
buffer manager, with a real compiled-and-run trace.

## Files

| File | Purpose |
|------|---------|
| `clocksweep.cpp` | The ClockSweep buffer pool (`fetch`/`pin`/`unpin` + victim selection) |
| `run_output.txt` | Captured output of `./clocksweep` |
| `.gitignore` | Excludes the compiled binary |

```bash
g++ -std=c++17 -O2 -o clocksweep clocksweep.cpp
./clocksweep
```

---

## The idea: approximate LRU without an ordered list

PostgreSQL keeps thousands of 8 KB pages in `shared_buffers`. True LRU would need
a doubly-linked list reordered on *every* page touch — and that list becomes a
global contention point under concurrency. ClockSweep avoids it:

- Buffers live in a **circular array**; a single **clock hand** points at the
  next candidate.
- Each frame has a **`usage_count` (0–5)**, incremented on every access (capped at
  5, mirroring PostgreSQL's `BM_MAX_USAGE_COUNT`).
- To find a victim the hand sweeps forward: a frame with `usage_count > 0` gets a
  **second chance** (decrement and skip); the first frame found at
  `usage_count == 0` (and not pinned) is **evicted**.

So "recently/frequently used" is encoded in the count, not in list position — an
O(1)-amortised approximation of LRU with no list maintenance per access.

```
        frame0        frame1        frame2        frame3
       ┌───────┐     ┌───────┐     ┌───────┐     ┌───────┐
 hand→ │ pg=1  │ ──▶ │ pg=2  │ ──▶ │ pg=3  │ ──▶ │ pg=4  │ ──┐
       │ use=2 │     │ use=2 │     │ use=1 │     │ use=1 │   │
       └───────┘     └───────┘     └───────┘     └───────┘   │
            ▲────────────────── circular ───────────────────┘
   sweep rule:  use>0 → use-- and skip (second chance)
                use==0 and !pinned → EVICT
```

---

## Real run

The driver uses a 4-frame pool and the access sequence
`1 2 3 4 1 2 5 1 2 3 4 5`. Pages **1** and **2** are touched again early, so they
build up `usage_count` and should outlive the colder pages. Captured output:

```
Access sequence: 1 2 3 4 1 2 5 1 2 3 4 5

[MISS]  page 1 loaded into frame 0
[MISS]  page 2 loaded into frame 1
[MISS]  page 3 loaded into frame 2
[MISS]  page 4 loaded into frame 3
[HIT]   page 1 in frame 0 usage=2
[HIT]   page 2 in frame 1 usage=2
[EVICT] page 3 from frame 2 (usage hit 0)
[MISS]  page 5 loaded into frame 2
[HIT]   page 1 in frame 0 usage=1
[HIT]   page 2 in frame 1 usage=1
[EVICT] page 4 from frame 3 (usage hit 0)
[MISS]  page 3 loaded into frame 3
[EVICT] page 1 from frame 0 (usage hit 0)
[MISS]  page 4 loaded into frame 0
[HIT]   page 5 in frame 2 usage=1

--- Buffer Pool State (hand=1) ---
Frame[0] page=4 usage=1
Frame[1] page=2 usage=0   <-- hand
Frame[2] page=5 usage=1
Frame[3] page=3 usage=0
Accesses=12  hits=5  misses=7  evictions=3  hit_ratio=41.6667%
```

### Walking the critical eviction (page 5 arrives)

State before: `f0=pg1(u2) f1=pg2(u2) f2=pg3(u1) f3=pg4(u1)`, hand at 0.

| Hand visits | usage_count | Action |
|-------------|-------------|--------|
| f0 (pg1) | 2 → 1 | second chance, skip |
| f1 (pg2) | 2 → 1 | second chance, skip |
| f2 (pg3) | 1 → 0 | second chance, skip |
| f3 (pg4) | 1 → 0 | second chance, skip |
| f0 (pg1) | 1 → 0 | second chance, skip |
| f1 (pg2) | 1 → 0 | second chance, skip |
| **f2 (pg3)** | **0** | **EVICT** ✓ |

Page 3 loses because it was the first to reach 0 *and* the hand reached it
first — exactly the "second chance" intuition. Crucially the two hot pages
(1, 2) were decremented but **survived this eviction**; that is ClockSweep
behaving like LRU. (They are eventually evicted later only because the workload
keeps cycling cold pages 3/4/5 and their counts decay with no further reuse.)

---

## Why PostgreSQL chose ClockSweep over LRU

| Property | LRU | ClockSweep |
|----------|-----|------------|
| Eviction quality | Exact recency | Near-optimal approximation |
| Data structure | Doubly-linked list + hashmap | Circular array + counter |
| Cost per access | O(1) but **list relink under a global lock** | O(1), just an atomic-ish counter bump |
| Concurrency | The LRU list is a hot contention point | No list to reorder → far less contention |
| Scan resistance | A full scan evicts everything | `usage_count` cap dampens scan flooding |

The decisive factor is the middle two rows: in a multi-backend server, an LRU
list must be re-linked on every buffer touch while holding a lock, serialising
all backends. ClockSweep only *reads/decrements a counter* during the sweep, so
the common path (a hit) touches no shared ordering structure at all.

**Scan resistance** matters too: a one-off sequential scan bumps each page's
count by just 1, while a genuinely hot page sits at 5 — so the hot page survives
several sweeps that wipe out the scanned pages.

---

## Design trade-offs & limitations

- **Approximation, not exact LRU.** A page touched many times long ago can tie a
  page touched once recently (both capped/decayed). ClockSweep accepts slightly
  worse eviction decisions in exchange for much cheaper, more scalable bookkeeping.
- **Sweep cost is bounded.** Worst case the hand makes ~2 full passes (every count
  decays to 0), so victim selection is O(capacity) worst case but O(1) amortised
  — my implementation caps the loop at `2 * capacity`.
- **Pinning.** A pinned frame (in active use by a backend) is never evicted; the
  sweep skips it. If *every* frame is pinned, no victim exists — handled by
  returning −1.

---

## Key learnings

- ClockSweep trades perfect recency for low overhead and low lock contention —
  the reason it scales across many concurrent backends where a global LRU list
  would not.
- A multi-valued `usage_count` (0–5), rather than a single reference bit, gives
  finer "hotness" gradation and naturally resists sequential-scan flooding.
- The clock hand means **no work is done on the hot path** (a hit just bumps a
  counter); the only sweeping happens when a victim is actually needed.
- This is the same victim-selection strategy as PostgreSQL's
  `StrategyGetBuffer()` in `src/backend/storage/buffer/freelist.c`.

---

### Reference

- Solution to `lab_sessions/lab_3.txt` (Advanced DBMS lab series).
- PostgreSQL buffer manager / ClockSweep: `src/backend/storage/buffer/freelist.c`, `BM_MAX_USAGE_COUNT`.
- Built with `g++` 13.3.0 `-std=c++17`, Ubuntu 24.04.
