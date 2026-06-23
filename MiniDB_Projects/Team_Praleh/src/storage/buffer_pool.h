#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// buffer_pool.h  –  LRU buffer pool manager
// ─────────────────────────────────────────────────────────────────────────────
#include "storage/page.h"
#include "storage/disk_manager.h"
#include <vector>
#include <list>
#include <unordered_map>
#include <mutex>

namespace minidb {

/**
 * BufferPool – manages a fixed set of in-memory Page frames.
 *
 * Replacement policy: LRU.
 *   - Front of lru_list_ = most-recently used frame.
 *   - Back  of lru_list_ = least-recently used; evicted first.
 *
 * A frame with pin_count > 0 cannot be evicted.
 */
class BufferPool {
public:
    BufferPool(size_t pool_size, DiskManager* dm);
    ~BufferPool();

    // Allocate a new disk page and load it into a frame.
    Page* NewPage(page_id_t* out_page_id);

    // Bring an existing disk page into a frame (or hit cache).
    Page* FetchPage(page_id_t page_id);

    // Caller is done with the page; optionally mark dirty.
    bool UnpinPage(page_id_t page_id, bool is_dirty);

    // Flush one page to disk (force-write, clears dirty).
    bool FlushPage(page_id_t page_id);

    // Flush every dirty page to disk.
    void FlushAll();

    // Remove frame from pool (page must be unpinned).
    bool DeletePage(page_id_t page_id);

private:
    bool FindVictim(frame_id_t* out_fid); // returns false if all pinned

    size_t                                    pool_size_;
    DiskManager*                              dm_;
    std::vector<Page>                         frames_;
    std::vector<int>                          pin_counts_;
    std::unordered_map<page_id_t, frame_id_t> page_table_; // page_id → frame_id
    std::list<frame_id_t>                     lru_;         // front=MRU, back=LRU
    std::unordered_map<frame_id_t,
        std::list<frame_id_t>::iterator>      lru_iter_;    // O(1) LRU update
    std::mutex                                latch_;
};

} // namespace minidb
