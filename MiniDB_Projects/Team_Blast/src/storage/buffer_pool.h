#pragma once

#include "storage/disk_manager.h"
#include "storage/page.h"
#include "common/types.h"
#include "common/config.h"
#include <unordered_map>
#include <vector>
#include <cstdint>

// ─── BufferPool ───────────────────────────────────────────────────────────────
//
// Keeps a fixed number of page frames in memory (BUFFER_POOL_SIZE).
// When a page is needed and no free frame exists, the Clock-Sweep algorithm
// (same logic as Lab 3) picks a victim frame to evict.
//
// Key operations:
//   pinPage(pid)      — load page into a frame, increment pin count
//   unpinPage(pid, dirty) — done using the page; dirty=true means it changed
//   flushPage(pid)    — write a dirty page back to disk
//   flushAll()        — flush every dirty page (called at commit / shutdown)
//
// A pinned page (pin_count > 0) is NEVER evicted.
// A dirty page is written to disk before its frame is reused.

class BufferPool {
public:
    explicit BufferPool(DiskManager& disk);
    ~BufferPool();

    // Fetch a page into the buffer pool and pin it.
    // Returns a pointer to the in-memory Page, or nullptr on failure.
    // Caller MUST call unpinPage() when done.
    Page* pinPage(PageID page_id);

    // Release a pin on a page.
    // If dirty=true, the page is marked dirty and will be written back
    // to disk when it is eventually evicted or when flushAll() is called.
    void unpinPage(PageID page_id, bool dirty = false);

    // Force a specific dirty page to disk immediately.
    void flushPage(PageID page_id);

    // Flush all dirty frames to disk (e.g., at checkpoint or shutdown).
    void flushAll();

    // Allocate a new page on disk and bring it into the buffer pool (pinned).
    // Returns a pointer to the new page, or nullptr on failure.
    Page* newPage(PageID& new_page_id);

private:
    // A Frame is one slot in the buffer pool.
    struct Frame {
        Page     page;                    // the actual page data
        PageID   page_id  = INVALID_PAGE_ID;
        int      pin_count = 0;           // how many callers are using this frame
        bool     dirty    = false;        // does this frame have unsaved changes?
        bool     ref_bit  = false;        // clock-sweep reference bit
        bool     valid    = false;        // is there a real page loaded here?
    };

    // Clock-sweep: find a frame to evict and return its index.
    // Returns -1 if all frames are pinned (pool is full).
    int clockSweep();

    // Write a frame's page to disk if dirty, then clear the frame metadata.
    void evictFrame(int frame_idx);

    DiskManager&              disk_;
    std::vector<Frame>        frames_;
    // page_id → frame index for fast lookup
    std::unordered_map<PageID, int> page_table_;
    int clock_hand_ = 0;   // current position of the clock hand
};
