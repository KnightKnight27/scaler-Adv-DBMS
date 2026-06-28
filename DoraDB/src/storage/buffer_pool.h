#pragma once

#include "common/config.h"
#include "storage/page.h"
#include "storage/disk_manager.h"

#include <unordered_map>
#include <vector>

// ============================================================
// BufferPool — manages a fixed pool of page frames in memory
//
// Uses Clock Sweep eviction (same idea as Lab03's accessPage).
// Pages are pinned while in use, unpinned when done.
// Dirty pages are flushed to disk before eviction.
// ============================================================

class BufferPool {
public:
    // Takes ownership of a DiskManager pointer. pool_size = number of frames.
    BufferPool(DiskManager* disk_mgr, int pool_size = BUFFER_POOL_SIZE);
    ~BufferPool();

    // No copies
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    // Fetch a page from buffer pool (reads from disk if not cached).
    // Pin count is incremented. Caller MUST call UnpinPage when done.
    Page* FetchPage(int page_id);

    // Decrement pin count. Mark dirty if caller modified the page.
    void UnpinPage(int page_id, bool is_dirty);

    // Force-write a dirty page to disk.
    void FlushPage(int page_id);

    // Allocate a new page on disk and bring it into the pool.
    // Returns a pinned page (caller must UnpinPage when done).
    Page* NewPage(int& page_id);

    // Flush all dirty pages to disk (used at shutdown / checkpoint).
    void FlushAll();

private:
    // Metadata for each buffer frame
    struct FrameInfo {
        int page_id = INVALID_PAGE_ID;  // which page is loaded here
        int pin_count = 0;               // how many users currently hold this page
        bool dirty = false;              // was it modified since last flush?
        bool ref_bit = false;            // for clock sweep (second chance)
    };

    DiskManager* disk_mgr_;

    // The actual page frames (the in-memory copies of pages)
    std::vector<Page> frames_;
    std::vector<FrameInfo> frame_info_;
    int pool_size_;

    // Lookup: page_id → frame index
    std::unordered_map<int, int> page_table_;

    // Clock sweep hand position
    int clock_hand_ = 0;

    // Find a frame to use (empty or evict via clock sweep).
    // Returns frame index, or -1 if all frames are pinned.
    int FindVictimFrame();
};
