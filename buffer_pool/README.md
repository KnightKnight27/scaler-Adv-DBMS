# Buffer Pool Manager: Architecture and Implementation Design

In a relational database management system (RDBMS), the disk acts as the primary non-volatile storage medium, while memory (RAM) is volatile but orders of magnitude faster. Because database tables can exceed the physical memory capacity of the host machine, the database cannot simply load all data into RAM. 

The **Buffer Pool Manager (BPM)** is the system component responsible for bridging this gap. It manages the caching of pages (blocks of data) in memory, coordinates read/write operations between execution engines and disk, and decides which pages to evict when memory is full.

---

## 1. Core Architecture and Data Structures

The Buffer Pool is a contiguous region of memory allocated as an array of memory blocks called **Frames**. Each frame is of size `PAGE_SIZE` (typically 4KB or 8KB, matching the filesystem block size).

```
   +-----------------------------------------------------------------------+
   |                        BUFFER POOL (in Memory)                        |
   |                                                                       |
   |  [ Frame 0 ]      [ Frame 1 ]      [ Frame 2 ]      [ Frame 3 ]       |
   |  +----------+     +----------+     +----------+     +----------+      |
   |  | Page 102 |     |  Page 45 |     | (Empty)  |     |  Page 87 |      |
   |  +----------+     +----------+     +----------+     +----------+      |
   +-------^-----------------^---------------------------------------------+
           |                 |
     +-----+-----------------+-----------------------+
     |                  PAGE TABLE                   |
     |  Maps: Page ID ------> Frame ID               |
     |        Page 102 -----> Frame 0                |
     |        Page 45  -----> Frame 1                |
     |        Page 87  -----> Frame 3                |
     +-----------------------------------------------+
                             ^
                             | (On Cache Miss)
   +-------------------------v---------------------------------------------+
   |                             DISK STORAGE                              |
   |  [Page 0]  [Page 1]  ...  [Page 45]  ...  [Page 87]  ...  [Page 102]  |
   +-----------------------------------------------------------------------+
```

### 1.1 In-Memory Metadata Components

#### A. Page Table
A hash table mapping logical **Page IDs** (which refer to logical blocks on disk) to physical **Frame IDs** (which refer to indexes in the buffer pool array).
- **Key**: `page_id_t`
- **Value**: `frame_id_t`
- **Purpose**: Fast lookup ($O(1)$) to determine if a page is already cached in memory.

#### B. Free List
A thread-safe list (usually a queue or stack) storing the indexes of frames that do not currently hold any disk pages. On startup, all frames are in the Free List.

#### C. Frame Metadata Table
An array of structs parallel to the buffer pool array containing metadata for each frame:
- **Page ID (`page_id_t`)**: The ID of the page currently residing in this frame.
- **Pin Count (`int`)**: Tracks how many active threads or queries are currently referencing this page. A page cannot be evicted if its pin count is greater than zero.
- **Dirty Flag (`bool`)**: Tracks whether the page has been modified since it was read from disk. If `true`, the page must be written back to disk before the frame can be reused.

---

## 2. API Design and Implementation Pseudocode

Here is a C++ representation of the core data structures and the Buffer Pool Manager interface.

```cpp
#pragma once
#include <iostream>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

using page_id_t = int32_t;
using frame_id_t = int32_t;
static constexpr size_t PAGE_SIZE = 4096; // 4 KB pages
static constexpr page_id_t INVALID_PAGE_ID = -1;

// Represents a physical page in memory
struct Page {
    char data[PAGE_SIZE];
    page_id_t page_id = INVALID_PAGE_ID;
    int pin_count = 0;
    bool is_dirty = false;
    std::shared_mutex rw_latch; // Read-write latch protecting the page's data
};

class Replacer {
public:
    virtual void Pin(frame_id_t frame_id) = 0;
    virtual void Unpin(frame_id_t frame_id) = 0;
    virtual bool Victim(frame_id_t *frame_id) = 0;
    virtual size_t Size() = 0;
    virtual ~Replacer() = default;
};
```

### 2.1 FetchPage
Retrieves a page by its ID. If the page is in the buffer pool, it pins the page and returns a pointer to it. If it is on disk, it finds a victim frame, writes the victim back to disk if dirty, reads the requested page into the frame, pins it, and returns the pointer.

```cpp
class BufferPoolManager {
private:
    size_t pool_size_;
    Page *pages_; // Array of Page objects (Buffer Pool)
    std::unordered_map<page_id_t, frame_id_t> page_table_;
    std::vector<frame_id_t> free_list_;
    Replacer *replacer_;
    std::mutex latch_; // Protects internal BPM metadata (page_table_, free_list_)

    // Simulates disk reading/writing
    void ReadPageFromDisk(page_id_t page_id, char *dst) { /* Disk I/O */ }
    void WritePageToDisk(page_id_t page_id, const char *src) { /* Disk I/O */ }

public:
    BufferPoolManager(size_t pool_size, Replacer *replacer) 
        : pool_size_(pool_size), replacer_(replacer) {
        pages_ = new Page[pool_size_];
        for (size_t i = 0; i < pool_size_; ++i) {
            free_list_.push_back(static_cast<frame_id_t>(i));
        }
    }

    ~BufferPoolManager() {
        delete[] pages_;
    }

    Page* FetchPage(page_id_t page_id) {
        std::lock_guard<std::mutex> guard(latch_);

        // 1. Check if the page is already cached in memory
        auto it = page_table_.find(page_id);
        if (it != page_table_.end()) {
            frame_id_t frame_id = it->second;
            Page *page = &pages_[frame_id];
            page->pin_count++;
            replacer_->Pin(frame_id); // Remove from eviction list
            return page;
        }

        // 2. Cache miss: Allocate a frame
        frame_id_t frame_id = -1;
        if (!free_list_.empty()) {
            frame_id = free_list_.back();
            free_list_.pop_back();
        } else {
            // Evict a page using the replacer
            if (!replacer_->Victim(&frame_id)) {
                return nullptr; // No evictable page (all pinned)
            }
            Page *victim_page = &pages_[frame_id];
            
            // If the victim page is dirty, flush it to disk
            if (victim_page->is_dirty) {
                WritePageToDisk(victim_page->page_id, victim_page->data);
                victim_page->is_dirty = false;
            }

            // Remove the victim page from the page table mapping
            page_table_.erase(victim_page->page_id);
        }

        // 3. Read requested page from disk into the allocated frame
        Page *page = &pages_[frame_id];
        page->page_id = page_id;
        page->pin_count = 1;
        page->is_dirty = false;
        ReadPageFromDisk(page_id, page->data);

        // Update tracking metadata
        page_table_[page_id] = frame_id;
        replacer_->Pin(frame_id); // Cannot evict while pin count > 0

        return page;
    }
```

### 2.2 UnpinPage
Decrements the pin count of a page. When the pin count drops to zero, the page is placed back in the eviction candidate list.

```cpp
    bool UnpinPage(page_id_t page_id, bool is_dirty) {
        std::lock_guard<std::mutex> guard(latch_);

        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) {
            return false;
        }

        frame_id_t frame_id = it->second;
        Page *page = &pages_[frame_id];

        if (page->pin_count <= 0) {
            return false;
        }

        if (is_dirty) {
            page->is_dirty = true;
        }

        page->pin_count--;
        if (page->pin_count == 0) {
            replacer_->Unpin(frame_id); // Page is now candidate for eviction
        }

        return true;
    }
```

### 2.3 FlushPage
Forces a page to write its changes to disk regardless of whether it is pinned.

```cpp
    bool FlushPage(page_id_t page_id) {
        std::lock_guard<std::mutex> guard(latch_);

        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) {
            return false;
        }

        frame_id_t frame_id = it->second;
        Page *page = &pages_[frame_id];
        
        if (page->is_dirty) {
            WritePageToDisk(page->page_id, page->data);
            page->is_dirty = false;
        }
        return true;
    }
};
```

---

## 3. Concurrency Control and Latching Protocols

To support high performance under concurrent operations, the BPM must handle thread safety with minimal locking overhead.

### 3.1 Global Latch vs. Latched Buckets
In the basic pseudocode, a single global mutex (`latch_`) protects the whole page table and free list. This creates a performance bottleneck in multi-threaded database engines (e.g., highly concurrent transaction processors).
- **Optimization (Partitioned Hash Table)**: The Page Table is split into $N$ isolated buckets, each containing its own lock. Lookups are hashed to a specific bucket, reducing thread lock contention dramatically.

### 3.2 Page-Level Read/Write Latches
It is critical to distinguish between **Locks** and **Latches**:
- **Lock**: Logical transaction isolation mechanism (e.g., locking a tuple in 2PL, held for transaction duration).
- **Latch**: Internal operational latch protecting structural integrity of memory (e.g., protecting the contents of a frame from concurrent modification while parsing a B+ Tree node). Held only for the duration of the operational read/write.

Each `Page` has an internal Read-Write Latch (`rw_latch`). 
- When an execution thread obtains a pointer to a page using `FetchPage`, it pins the page but *does not* hold the global BPM latch.
- Before reading data from the page, it acquires a shared read latch (`page->rw_latch.lock_shared()`).
- Before updating data on the page, it acquires an exclusive write latch (`page->rw_latch.lock()`).
- This allows multiple threads to read the same page frame in memory concurrently.

---

## 4. Page Replacement/Eviction Policies

When the buffer pool runs out of space, the database must evict a page whose pin count is $0$.

### 4.1 Clock Sweep Algorithm
An approximation of the Least Recently Used (LRU) algorithm that operates with $O(1)$ complexity and very low metadata overhead.

```
                  Clock Hand
                      |
                      v
      +-----------+-----------+-----------+-----------+
      |  Frame 0  |  Frame 1  |  Frame 2  |  Frame 3  |
      |  Ref = 1  |  Ref = 0  |  Ref = 1  |  Ref = 0  |  (Pinned)
      +-----------+-----------+-----------+-----------+
```

1. **Structure**: Frames are viewed logically as a circular ring.
2. **Metadata**: Each frame has a **Reference Bit** (ref bit), set to `1` whenever the page is accessed.
3. **Execution**:
   - The clock hand sweeps through frames in a circle.
   - If a page has a pin count $>0$, the hand skips it.
   - If a page is unpinned (pin count $=0$) and its ref bit is `1`, the hand sets the ref bit to `0` and advances.
   - If a page is unpinned and its ref bit is `0`, this page is chosen as the **Victim**. The clock hand stops here.

### 4.2 LRU-K Algorithm
The basic LRU algorithm is vulnerable to the **Sequential Scan Pollution** problem. If a query runs a full-table scan on a dataset larger than the buffer pool, it will evict all pages in memory to read pages it will only use once.

**LRU-K** addresses this by tracking the time of the last $K$ references to a page:
- The distance between the current time and the $K$-th backward reference is calculated.
- The page with the largest backward distance (i.e., accessed less frequently in the past) is evicted.
- Pages accessed fewer than $K$ times are given an infinite backward distance, prioritizing them for eviction.
- **LRU-2** is the standard industry choice for high-performance databases, balance-weighting recency and frequency.

---

## 5. Advanced System Optimizations

### 5.1 Prefetching
Instead of waiting for an execution engine to request a page, the BPM anticipates requests.
- **Sequential Scan Prefetch**: If the system detects pages are being read sequentially (e.g., Page 12, 13, 14), it pre-emptively fetches Pages 15, 16, and 17 in the background using a separate worker thread.
- **Index Scan Prefetch**: In B+ Trees, when traversing a node, the BPM pre-emptively fetches the children pointers.

### 5.2 Buffer Pool Bypass (Direct I/O)
For operations like sequential bulk exports or index scans that process massive datasets once:
- The BPM can assign a small ring-buffer specifically for that query, bypassing the main buffer pool entirely.
- This prevents the eviction of index-root pages and hot metadata.

### 5.3 Double Buffering (Asynchronous Dirty Page Flushing)
Writing a page back to disk blocks execution if done on the critical path of page eviction.
- To prevent eviction latency, a background process (**Page Writer**) continually scans the pool, locks dirty pages with pin count $0$, writes them to disk, clears their dirty flags, and returns them to a clean state.
- Thus, the BPM almost always evicts clean pages, eliminating disk-write latency from the client's request pathway.
