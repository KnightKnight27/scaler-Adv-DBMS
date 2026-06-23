#pragma once

#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "common/types.h"
#include "storage/disk_manager.h"
#include "storage/page.h"

namespace minidb {

// BufferPool caches a fixed number of pages in memory and is the only path
// through which higher layers touch pages. Replacement uses the PostgreSQL
// clock-sweep policy (from lab_3): each frame has a usage_count (0..MAX) that is
// bumped on access and decremented by a rotating "hand"; a frame is evicted when
// it is unpinned and its count reaches 0. This approximates LRU without an
// ordered list and resists sequential-scan flooding.
//
// Buffer policy is STEAL + NO-FORCE: dirty pages may be written back on eviction
// (steal) and are not forced at commit; this is what makes the WAL/recovery in
// M5 necessary.
class BufferPool {
public:
    BufferPool(std::size_t capacity, DiskManager* disk);

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    // Pin page `pid` (loading it from disk on a miss) and return it. The caller
    // must unpin_page() when done. Throws if every frame is pinned.
    Page* fetch_page(PageId pid);

    // Allocate a brand-new page on disk, pin it, and return it (zeroed).
    Page* new_page(PageId& out_pid);

    // Release one pin on `pid`; pass dirty=true if the page was modified.
    void unpin_page(PageId pid, bool dirty);

    // Write a single page to disk if dirty (does not unpin).
    bool flush_page(PageId pid);

    // Write every dirty page to disk.
    void flush_all();

    // --- statistics (for the M1 demo and benchmarks) ---
    std::size_t hits() const      { return hits_; }
    std::size_t misses() const    { return misses_; }
    std::size_t evictions() const { return evictions_; }

private:
    // Per-frame bookkeeping kept separate from the raw Page bytes.
    struct Frame {
        Page page;
        int  pin_count   = 0;
        bool dirty       = false;
        int  usage_count = 0;
    };

    // Return a frame index ready to be (re)used: a free frame if available,
    // otherwise a clock-sweep victim that has been flushed and unmapped.
    int grab_frame();
    int clock_sweep();                 // returns victim frame index
    void flush_frame_locked(int idx);  // write frame to disk if dirty

    std::size_t                       capacity_;
    DiskManager*                      disk_;
    std::vector<Frame>                frames_;
    std::unordered_map<PageId, int>   page_table_;  // page_id -> frame index
    std::vector<int>                  free_list_;   // unused frame indices
    int                               hand_ = 0;    // clock hand
    std::mutex                        mutex_;        // makes the pool thread-safe (M4)

    std::size_t hits_ = 0, misses_ = 0, evictions_ = 0;
};

} // namespace minidb
