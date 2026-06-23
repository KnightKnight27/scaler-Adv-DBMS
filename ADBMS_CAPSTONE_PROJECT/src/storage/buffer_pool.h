#pragma once
#include "storage/disk_manager.h"
#include "storage/page.h"
#include <unordered_map>
#include <list>
#include <mutex>
#include <vector>

namespace minidb {

class BufferPool {
public:
    BufferPool(size_t pool_size, DiskManager* disk_manager);
    ~BufferPool();

    // Fetch the requested page from the buffer pool.
    Page* FetchPage(PageId page_id);

    // Unpin the target page from the buffer pool.
    bool UnpinPage(PageId page_id, bool is_dirty);

    // Flush the target page to disk.
    bool FlushPage(PageId page_id);

    // Creates a new page in the buffer pool and disk.
    Page* NewPage(PageId* page_id);

    // Flush all pages
    void FlushAllPages();

private:
    // Find a free frame for replacement
    bool FindFreeFrame(int* frame_id);

    size_t pool_size_;
    DiskManager* disk_manager_;
    std::vector<Page> pages_;
    
    // Page table maps PageId -> FrameId
    std::unordered_map<PageId, int> page_table_;
    
    // LRU List containing FrameIds of unpinned pages
    std::list<int> lru_list_;
    
    // Lock for concurrent access
    std::mutex latch_;
};

} // namespace minidb
