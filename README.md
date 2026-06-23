# scaler-Adv-DBMS

Coursework for the Scaler Advanced DBMS module.

Lab numbering follows the official `lab_sessions/lab_N.txt` definitions
in the assignment repo.

| Lab | Topic | Path on this branch |
| --- | --- | --- |
| 1 | File I/O in C++ — strace kernel journey | _(not in this branch)_ |
| 2 | SQLite3 internals + PostgreSQL vs SQLite report | `submit/...` branch |
| 3 | Clock-sweep buffer pool replacement | [`lab3/`](lab3/) |
| 4 | Red-Black Tree (Part 1) & Full B-Tree (Part 2) | [`Lab-4/`](Lab-4/) |
| 5 | Shunting-Yard + minimal SQL SELECT parser | [`Lab-5/`](Lab-5/) |
| 6 | Transaction Manager — MVCC + 2PL + deadlock detection | [`Lab-6/`](Lab-6/) |

> **Note on naming:** the Red-Black Tree and B-Tree (official Lab 4,
> Parts 1 & 2) were previously in folders named `Lab-5/` and `Lab-6/`.
> They have been moved into `Lab-4/Part1-RBT/` and `Lab-4/Part2-BTree/`
> so the folder numbers match the official lab definitions. The folders
> `Lab-5/` and `Lab-6/` now hold the actual Lab 5 and Lab 6 work.

---

## Lab 3 — Clock-Sweep Buffer Pool

A small buffer pool manager that uses the **clock-sweep (second-chance) page
replacement** algorithm. The buffer pool sits between query code and disk:
it caches a bounded number of pages in memory and decides which page to
evict when the cache fills up.

### Layout

```
lab3/
├── pom.xml
├── src/
│   ├── main/java/com/scaler/adbms/lab3/
│   │   ├── Page.java                     # one frame's worth of data + bookkeeping
│   │   ├── DiskManager.java              # tiny disk simulator (file or in-memory)
│   │   ├── ClockSweepReplacer.java       # the replacement algorithm
│   │   ├── BufferPoolManager.java        # orchestrates frames, page table, eviction
│   │   ├── BufferPoolStats.java          # hit / miss / eviction counters
│   │   ├── Demo.java                     # hand-runnable walkthrough
│   │   └── internal/SelfCheck.java       # dep-free assertions (no JUnit needed)
│   └── test/java/com/scaler/adbms/lab3/
│       ├── ClockSweepReplacerTest.java   # 9 JUnit cases for the replacer
│       └── BufferPoolManagerTest.java    # 9 JUnit cases for the pool
└── README.md
```

### The algorithm

Each frame slot in the pool tracks two flags the replacer cares about:

- **`evictable`** — only evictable frames are eviction candidates. A frame
  becomes non-evictable as soon as someone pins it (`fetchPage` /
  `newPage`) and goes back to evictable when its pin count returns to zero
  (`unpinPage`).
- **`refBit`** — set whenever the page in this frame is touched. A single
  sweep clears the bit instead of evicting — that's the "second chance".

Victim selection advances a clock hand forward. For each evictable frame:

```
  refBit == 1  -> clear it, advance       (spared this round)
  refBit == 0  -> evict, return frame id
```

Pinned (non-evictable) frames are skipped. In the worst case every
evictable frame has `refBit == 1`, so the hand may need one full pass to
clear bits and another to pick the victim — bounded at `2 * N` steps for a
pool of `N` frames.

### Buffer pool API

| Method | What it does |
| --- | --- |
| `newPage()` | allocate a fresh page on disk, pin it, return the `Page` |
| `fetchPage(pageId)` | pin and return a cached page; load from disk on miss |
| `unpinPage(pageId, isDirty)` | release one pin; once `pinCount==0` the frame becomes evictable |
| `flushPage(pageId)` | write a single page back to disk and clear its dirty flag |
| `flushAll()` | flush every cached page |
| `deletePage(pageId)` | drop the page from the pool & deallocate on disk; refuses while pinned |
| `stats()` | snapshot of hit / miss / eviction / free-frame / cached-page counts |

When eviction is needed the manager:

1. Asks the replacer for a victim frame.
2. If the victim's page is **dirty**, writes its contents to disk first.
3. Removes the page from the page table and reuses the frame.

### Running

The project is a standard Maven build, but it works fine with raw `javac`
too (the `internal/SelfCheck.java` runner has no external dependencies).

**Maven (preferred):**

```bash
cd lab3
mvn test            # run the JUnit suite
mvn exec:java       # run the Demo
```

**Without Maven (just JDK 17+):**

```bash
cd lab3
javac -d target/classes src/main/java/com/scaler/adbms/lab3/*.java \
                        src/main/java/com/scaler/adbms/lab3/internal/*.java

# walkthrough
java -cp target/classes com.scaler.adbms.lab3.Demo

# self-checks (no JUnit needed)
java -cp target/classes com.scaler.adbms.lab3.internal.SelfCheck
```

### What the demo shows

`Demo.java` runs in two parts:

- **Part A** drives the `ClockSweepReplacer` in isolation. It shows the
  default clock order with all ref bits cleared, then sets the ref bit on
  one frame and shows that frame being skipped by the next sweep.
- **Part B** opens a pool of 3 frames over an in-memory disk, fills it
  with three pages, allocates two more (forcing two evictions), then
  reloads an evicted page from disk and hits a cached one. The final
  `BufferPoolStats` line prints hit count, miss count, evictions, and
  hit rate.

### Notes

- Page size is fixed at `4096` bytes (`Page.PAGE_SIZE`).
- All public methods of `BufferPoolManager` are `synchronized` on the
  manager itself so the page table, free list, and replacer stay
  consistent under concurrent access.
- `DiskManager` has an in-memory mode (used by the demo and tests) and a
  file-backed mode (pass a `Path` to the constructor).
