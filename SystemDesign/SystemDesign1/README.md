# Design a Buffer Pool

**Name:** Shah Musharaf ul Islam
**College ID:** 24bcs10447
**OS:** Arch Linux | **Shell:** zsh

---

## What & Why

A buffer pool is an in-memory cache of disk pages. Databases can't afford to hit disk on every read or write — the buffer pool keeps hot pages in RAM and evicts cold ones when space runs out. This is the single most important component for I/O performance in any disk-oriented DBMS.

This implementation uses an **LRU eviction policy** with O(1) lookup and eviction via `std::list` + `std::unordered_map`.

---

## Project Structure

```
SystemDesign1/
├── BufferPool.h      # Frame struct + BufferPool class declaration
├── BufferPool.cpp    # Implementation (LRU, eviction, flush)
├── main.cpp          # Demo — fill, pin/unpin, evict, flush
├── Makefile
├── .gitignore
└── README.md
```

---

## Implementation

### Frame struct

Each frame in the pool holds a page ID, dirty flag, pin count, and the page data:

```cpp
struct Frame {
    int      page_id  = -1;
    bool     dirty    = false;
    int      pin_count = 0;
    std::string data;         // simplified — real DBs use a fixed-size byte array
};
```

### LRU eviction

The eviction logic walks the LRU list from the back (least recently used) and picks the first unpinned page. If that page is dirty, it gets flushed to disk first.

```cpp
bool BufferPool::evict() {
    for (auto rit = lru_.rbegin(); rit != lru_.rend(); ++rit) {
        int pid = *rit;
        Frame& f = frames_[pid];
        if (f.pin_count == 0) {
            if (f.dirty) {
                diskWrite(f);
            }
            // remove from LRU + frames_
            lru_.erase(lru_map_[pid]);
            lru_map_.erase(pid);
            frames_.erase(pid);
            return true;
        }
    }
    return false;  // all pages pinned
}
```

### O(1) LRU touch

Every time a page is accessed, it moves to the front of the LRU list. The `unordered_map` stores iterators into the list so both insert and erase are O(1).

```cpp
void BufferPool::touchLRU(int page_id) {
    auto it = lru_map_.find(page_id);
    if (it != lru_map_.end()) {
        lru_.erase(it->second);
    }
    lru_.push_front(page_id);
    lru_map_[page_id] = lru_.begin();
}
```

---

## Build & Run

```bash
make
./buffer_pool
```

```bash
make clean    # remove binary
```

---

## Output

```
==========================================
 Buffer Pool Manager  —  3 frame demo
==========================================

--- Step 1: Fill the pool (pages 1, 2, 3) ---
  fetchPage(1): cache MISS
  fetchPage(2): cache MISS
  fetchPage(3): cache MISS

  Pool state [after filling]  (3/3 frames)
  ----------------------------------------------------
  page_id   pin_cnt   dirty     data
  ----------------------------------------------------
  3         1         no        data_of_page_3
  2         1         no        data_of_page_2
  1         1         no        data_of_page_1

--- Step 2: Unpin pages 1 and 2 (mark page 2 dirty) ---
  unpinPage(1, dirty=0)
  unpinPage(2, dirty=1)

  Pool state [after unpins]  (3/3 frames)
  ----------------------------------------------------
  page_id   pin_cnt   dirty     data
  ----------------------------------------------------
  3         1         no        data_of_page_3
  2         0         yes       data_of_page_2
  1         0         no        data_of_page_1

--- Step 3: Fetch page 4 (pool is full, must evict) ---
  fetchPage(4): cache MISS
    [EVICT] page 1

  Pool state [after eviction + fetch]  (3/3 frames)
  ----------------------------------------------------
  page_id   pin_cnt   dirty     data
  ----------------------------------------------------
  4         1         no        data_of_page_4
  3         1         no        data_of_page_3
  2         0         yes       data_of_page_2

--- Step 4: Fetch page 3 again (cache hit) ---
  fetchPage(3): cache HIT

  Pool state [after cache hit]  (3/3 frames)
  ----------------------------------------------------
  page_id   pin_cnt   dirty     data
  ----------------------------------------------------
  3         2         no        data_of_page_3
  4         1         no        data_of_page_4
  2         0         yes       data_of_page_2

--- Step 5: Modify page 4, then flush it ---
  fetchPage(4): cache HIT
  unpinPage(4, dirty=1)
  unpinPage(4, dirty=0)
    [DISK WRITE] page 4 -> "UPDATED_page_4"
  flushPage(4): done

  Pool state [after flush]  (3/3 frames)
  ----------------------------------------------------
  page_id   pin_cnt   dirty     data
  ----------------------------------------------------
  4         0         no        UPDATED_page_4
  3         2         no        data_of_page_3
  2         0         yes       data_of_page_2

--- Step 6: Flush all ---
  unpinPage(3, dirty=1)
  unpinPage(3, dirty=0)
  flushAll():
    [DISK WRITE] page 3 -> "data_of_page_3"
    [DISK WRITE] page 2 -> "data_of_page_2"

  Pool state [final state]  (3/3 frames)
  ----------------------------------------------------
  page_id   pin_cnt   dirty     data
  ----------------------------------------------------
  4         0         no        UPDATED_page_4
  3         0         no        data_of_page_3
  2         0         no        data_of_page_2

==========================================
```

---

## Notes

- **Pin count matters.** A page can't be evicted while someone is using it. The pin/unpin pattern is basically reference counting — same idea databases like PostgreSQL use in their real buffer managers.
- **LRU with `std::list` + `std::unordered_map`** gives O(1) for both "touch" (move to front) and "find victim" (grab from back). This is the classic trick — LeetCode 146 is literally this data structure.
- **Dirty pages must be written back before eviction.** If we evict a dirty page without flushing, that data is gone forever. The flush-before-evict logic is the simplest form of a *write-back* policy.
