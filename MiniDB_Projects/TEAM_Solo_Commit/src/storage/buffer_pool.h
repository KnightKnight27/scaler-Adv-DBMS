// MiniDB - BufferPool: a fixed set of in-memory frames over the on-disk pages.
//
// Eviction uses the CLOCK-SWEEP (second-chance) policy I implemented in Lab 3: each frame
// carries a reference bit, a hand sweeps the frames in a circle, clears a set ref bit and
// moves on (a "second chance"), and evicts the first unpinned frame whose ref bit is already
// clear. Pinned frames (in active use) are never evicted; dirty victims are flushed first.
// This is the same strategy PostgreSQL's buffer manager uses.
#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "disk_manager.h"
#include "page.h"

namespace minidb {

class BufferPool {
public:
    BufferPool(DiskManager* disk, size_t num_frames);

    // Fetch (loading from disk on a miss) and PIN the page. Returns nullptr only if every
    // frame is pinned and none can be evicted. Caller must Unpin when done.
    Page* FetchPage(int page_id);

    // Allocate a brand-new page on disk, pin it, and return it (page_id set in *out_id).
    Page* NewPage(int* out_id);

    // Release a pin. Mark dirty if the caller modified the page.
    bool Unpin(int page_id, bool is_dirty);

    void FlushPage(int page_id);
    void FlushAll();

    // Diagnostics used by the demo/benchmarks.
    struct Stats { uint64_t hits = 0, misses = 0, evictions = 0; };
    Stats stats() const { return stats_; }

private:
    struct Frame {
        int page_id = INVALID_PAGE_ID;
        Page page;
        bool dirty = false;
        int pin_count = 0;
        bool ref_bit = false;
        bool occupied = false;
    };

    int FindFrameForLoad();   // a free frame, or a clock-sweep victim (flushing if dirty)
    int ClockEvict();

    DiskManager* disk_;
    std::vector<Frame> frames_;
    std::unordered_map<int, int> table_;  // page_id -> frame index
    size_t hand_ = 0;
    Stats stats_;
    std::mutex latch_;
};

}  // namespace minidb
