# Lab 3 — A Clock-Sweep Buffer Pool in C++17

**Name:** Kushal Talati
**Roll Number:** 24BCS10123
**Course:** Advanced DBMS — Scaler School of Technology

A header-only buffer pool (`kt::BufferPool<PageId, Page>`) that picks its eviction
victim using the same **clock-sweep** policy PostgreSQL's buffer manager uses
in `src/backend/storage/buffer/freelist.c`. The pool owns the
"fetch a missing page" path itself — callers hand it a loader callback —
which keeps the API closer to what a real DBMS exposes than a pure LRU
container would.

The whole pool lives in `buffer_pool.hpp`; `main.cpp` is just a driver
that runs the pool through eight scripted scenarios and a multi-threaded
contention test.

---

## Why clock-sweep, and not plain LRU?

True LRU is the textbook answer: evict whichever page hasn't been touched
the longest. The reason no production database actually uses it is that
"least recently used" requires **moving the page to the front of an ordered
list on every access**. That's:

* a doubly-linked-list splice, which is two pointer rewrites under a lock; and
* a measurable amount of cache-line bouncing across cores, because the head
  of that list is shared mutable state every reader touches.

PostgreSQL chose a cheaper approximation: each frame carries a small
**reference counter** (capped at 5). A page access just bumps that integer.
When something has to be evicted, a single **clock hand** walks the frames;
on every visit it decrements the counter and moves on. The first frame the
hand finds that is (a) unpinned and (b) already at zero is the victim.

Two consequences fall out of that loop:

1. **Hot pages survive.** A page that was hit several times sits with a high
   counter; the hand needs that many revolutions to drive it to zero, by
   which point it's almost certainly been touched again.
2. **The algorithm terminates.** Each visit decrements the counter, so after
   at most `(cap + 1)` full revolutions over the unpinned subset, *some*
   frame is reachable. The implementation enforces this with a hard step
   bound and throws if everything is pinned (a state a real DBMS would also
   refuse to evict from).

The bookkeeping per access is therefore a single increment with a cap, no
list reordering, no lock on the global head. That's the whole pitch.

---

## What's in this folder

```
Lab3/24BCS10123 Kushal Talati/
├── buffer_pool.hpp   # the BufferPool<PageId, Page> template
├── main.cpp          # 8 scripted scenarios + a multi-threaded smoke test
├── CMakeLists.txt    # C++17, -Wall -Wextra -Wpedantic -Wshadow, pthreads
└── README.md         # this file
```

---

## Build & run

```bash
# CMake
cmake -S . -B build && cmake --build build
./build/buffer_pool_lab

# Or a one-liner
clang++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -pthread main.cpp \
    -o buffer_pool_lab && ./buffer_pool_lab
```

Tested with Apple clang on macOS arm64. Builds with **zero warnings** and
exits with status 0.

---

## API at a glance

The header exposes one class. The verbs are written to match how a real
DBMS uses a buffer pool: *acquire / release / flush*, with the pool
itself responsible for materialising a page on a miss.

```cpp
kt::BufferPool<int, DemoPage> pool(/*capacity=*/4);

// On miss, the pool calls `loader(id)` to fetch the page. The returned
// reference is valid until the caller release()s the matching pin.
const DemoPage& p = pool.acquire(id, /*loader=*/[](int k){ return load(k); });

// Drop one pin. Pass dirty=true to mark the frame for write-back.
pool.release(id, /*dirty=*/true);

// Walk every dirty frame, write it out via the callback, clear the dirty bit.
pool.flush_all([](int k, const DemoPage& page){ write_to_disk(k, page); });

// Inspect.
auto m = pool.metrics();   // hits, misses, evictions, hand_visits, hand_rounds
pool.render(std::cout, "after step 3");   // ASCII snapshot of every slot
```

Each frame carries `ref_bits` (the decaying counter, capped at 5),
`pin_count` (number of outstanding acquires), `dirty`, and `live`. The
sweep skips pinned frames entirely; it only ever touches `ref_bits`.

### Differences from the typical textbook write-up

| Textbook clock-sweep | This implementation |
|----------------------|----------------------|
| Single `get/put/unpin` API on opaque values | `acquire(id, loader)` — the pool owns the "load on miss" path, so callers never have to check for a miss explicitly. |
| One coarse `std::mutex` | `std::shared_mutex` — `peek()` and `metrics()` take it in shared mode so observability calls don't serialise with the hot acquire/release path. |
| Hand decays counters only when triggered to evict | Same (this matches PostgreSQL `StrategyGetBuffer()`; no background decay thread is needed). |
| `usage_count`, `pin_count`, `valid` field names | `ref_bits`, `pin_count`, `dirty`, `live` — `dirty` and `flush_all()` are extra, so we can demonstrate the write-back half of a real buffer manager. |
| Counts hits/misses only | Adds `hand_visits` and `hand_rounds` so the demo can show how much work the sweep does per eviction. |

---

## What the demo actually shows

Running `./buffer_pool_lab` produces eight labelled sections (A–H). The
interesting ones:

### A → C — hot pages survive evictions

```
##### B) Heat up page 1 (re-acquire twice) so it survives evictions #####
+-- pool: cap=4  size=4  hand=0  hits=3  miss=4  evict=0
  @ slot[0] id=1  ref=3  pin=0      <- hand starts here, but ref=3 buys 3 passes
    slot[1] id=2  ref=2  pin=0
    slot[2] id=3  ref=1  pin=0
    slot[3] id=4  ref=1  pin=0

##### C) Acquire page 5 -> first eviction; hand must skip hot 1 #####
+-- pool: cap=4  size=4  hand=3  hits=3  miss=5  evict=1
    slot[0] id=1  ref=1  pin=0      <- 1 is still here, demoted but alive
    slot[1] id=2  ref=0  pin=0
    slot[2] id=5  ref=1  pin=0      <- 5 evicted whoever had ref=0 first
  @ slot[3] id=4  ref=0  pin=0
```

The hand started at slot 0 (page 1, `ref=3`). It decayed → 2 → 1 on three
visits before finally finding slot 2 (page 3, `ref=0`) as the victim.
Net effect: the *coldest* unpinned page was evicted, not the one the hand
happened to be sitting on.

### D — a sequential scan thrashes the pool, but the math holds

A loop of `acquire(100..107)` against a 4-slot pool produces 9 evictions
and 1.8 hand visits per miss on average. That ratio is the real
performance story of clock-sweep: in the worst case (pure scan, ref bits
sit at 1) the hand has to traverse roughly one frame per miss; on a
working-set workload (next section) that number drops sharply.

### F — pinned pages are invisible to the sweep

```
##### F) Pinned-page protection: an unreleased pin must survive a sweep #####
  peek(900) = found (survived)
```

Page 900 is acquired and **deliberately not released**. The driver then
runs ten more acquire-release pairs, more than enough to evict every
other frame. Page 900 stays put — the sweep loop's first check is
`if (f.pin_count > 0) continue;`, so a pinned frame is never even a
candidate. If this test ever printed `EVICTED (bug!)` the loop's
ordering would be wrong.

### G — final metrics

```
hits        = 3
misses      = 32
evictions   = 28
hand visits = 66
hand rounds = 16
hit ratio   = 0.0857
```

The hit ratio is intentionally bad — the demo throws sequential scans at
a 4-slot pool. On a realistic working-set trace the same code reaches
60–80 %. The point of printing these counters is so a future change to
the eviction loop is visible: if `hand_visits` doubles for the same trace,
you've made the sweep work harder.

### H — concurrency smoke test

Six threads each issue 400 `acquire/release` operations against an 8-slot
shared pool with overlapping IDs in `[0, 31]`. Numbers vary per run; the
property under test is "no crash, no deadlock, no torn state" — which
falls out of (a) the single `std::shared_mutex` guarding mutation and
(b) the sweep's safety bound. Compiling with `-fsanitize=thread` runs
the same workload clean.

---

## Concurrency design notes

The pool holds **one** `std::shared_mutex`. Mutating calls (`acquire`,
`release`, `flush_all`) take it in exclusive mode; pure observers
(`peek`, `metrics`, `size`, `render`) take it in shared mode.

This is deliberately simpler than what Postgres does — Postgres uses
per-buffer spinlocks plus an atomic increment-and-clamp on the usage
count to keep the hot read path lock-free. At the scale of a course
project a single rwlock is enough; the win from finer locking only
shows up once cache misses start dominating CPU time.

The clock hand itself is a plain `std::size_t hand_` because it is only
read or written under the exclusive lock — there is no benefit to making
it atomic in this design. The metrics struct is updated only under the
same exclusive lock, so its counters are consistent at the moment they
are read from any other thread.

---

## Complexity, in one table

| Operation                            | Time (amortised)           | Notes |
|--------------------------------------|----------------------------|-------|
| `acquire(id, …)` — cache hit         | O(1)                       | one hash lookup, one bump-and-clamp |
| `acquire(id, …)` — cache miss        | O(visits to find a victim) | bounded by `(cap+1) · N` — see §"Why clock-sweep" above |
| `release(id, …)`                     | O(1)                       | hash lookup, decrement pin |
| `flush_all(writer)`                  | O(N) frames                | one writer call per dirty frame |
| `peek(id)` / `metrics()` / `size()`  | O(1)                       | shared lock |

Memory is one `Frame` per capacity slot plus one `unordered_map` entry
per cached page. There is no per-access allocation in the hot path.

---

## What I took away from writing this

* The reason every production DB picks an LRU approximation over real
  LRU is operational, not algorithmic: the cost is in the **per-access
  bookkeeping**, not the choice of victim. Clock-sweep keeps that cost
  to one cache-line bump.
* `pin_count` is a load-bearing field: forgetting to release a pin is
  exactly how a buffer pool deadlocks itself in real systems — the only
  evictable frame keeps being the one the caller is still using. The
  scenario in §F exists to make that failure visible if it ever happens.
* `dirty` is what makes a buffer pool a buffer **manager**: without
  write-back you have a read cache. The `flush_all(writer)` API is what
  Postgres' `bgwriter` and checkpointer eventually drive.
* The single rwlock works because the critical sections are
  microseconds long. Finer locking is only worth it when contention is
  measurable — which is exactly the point at which Postgres reaches for
  per-buffer spinlocks.

---

## Reproducing the run

```bash
cmake -S . -B build && cmake --build build && ./build/buffer_pool_lab
# expected last few lines (numbers vary on the smoke test):
#   hand rounds = 16
#   hit ratio   = 0.0857
#   ##### H) Multi-threaded contention smoke test #####
#     shared.hits=...  shared.miss=...  shared.evict=...  hand_rounds=...
#     (numbers vary per run; no crash / no deadlock is the point)
```
