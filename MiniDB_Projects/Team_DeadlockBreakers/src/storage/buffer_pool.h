#pragma once

#include "common/types.h"
#include "storage/disk_manager.h"
#include "storage/page.h"

#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace minidb {

/*
 * BufferPoolManager: caches pages in memory using LRU replacement.
 * All page access in MiniDB goes through the buffer pool.
 */
class BufferPoolManager {
public:
    BufferPoolManager(uint32_t pool_size, DiskManager* disk_mgr);
    ~BufferPoolManager();

    // Fetch a page from the buffer pool (reads from disk if not cached).
    // Pin count is incremented. Caller MUST call UnpinPage when done.
    Page* FetchPage(page_id_t page_id);

    // Create a new page in the buffer pool (allocates on disk).
    Page* NewPage(page_id_t& new_page_id);

    // Unpin a page. If is_dirty, page will be written back on eviction.
    bool UnpinPage(page_id_t page_id, bool is_dirty);

    // Write a specific page back to disk
    bool FlushPage(page_id_t page_id);

    // Write all pages back to disk
    void FlushAll();

    // Delete a page from the buffer pool
    bool DeletePage(page_id_t page_id);

    DiskManager* GetDiskManager() const { return disk_mgr_; }

private:
    struct FrameInfo {
        Page     page;
        bool     is_dirty   = false;
        uint32_t pin_count  = 0;
        page_id_t page_id   = INVALID_PAGE_ID;
    };

    // Evict the least-recently-used unpinned page. Returns frame index, or -1.
    int EvictFrame();

    uint32_t                              pool_size_;
    DiskManager*                          disk_mgr_;
    std::vector<FrameInfo>                frames_;

    // page_id → frame index
    std::unordered_map<page_id_t, uint32_t> page_table_;

    // LRU list: front = most recently used, back = least recently used
    // Stores frame indices. Only UNPINNED frames are in the LRU list.
    std::list<uint32_t>                    lru_list_;
    std::unordered_map<uint32_t, std::list<uint32_t>::iterator> lru_map_;

    // Free frames (not holding any page)
    std::list<uint32_t>                    free_list_;

    std::mutex                             latch_;

    void LRURemove(uint32_t frame_id);
    void LRUInsert(uint32_t frame_id);
};

} // namespace minidb
