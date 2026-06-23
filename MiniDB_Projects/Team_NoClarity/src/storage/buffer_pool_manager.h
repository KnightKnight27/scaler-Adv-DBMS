#ifndef BUFFER_POOL_MANAGER_H
#define BUFFER_POOL_MANAGER_H

#include "common/config.h"
#include "storage/page.h"
#include "storage/disk_manager.h"
#include "storage/clock_replacer.h"
#include <unordered_map>
#include <mutex>

namespace minidb {

/**
 * Manages frame cache loaded in memory, coordinating frame retrieval and eviction cycles.
 */
class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager* disk_manager);
    ~BufferPoolManager();

    // Fetches page from disk or cache pool, pin count gets incremented
    Page* FetchPage(page_id_t page_id);
    
    // Decrements pin count on the page, marking dirty state if updated
    bool UnpinPage(page_id_t page_id, bool is_dirty);
    
    // Writes page changes out to physical disk if page dirty flag matches
    bool FlushPage(page_id_t page_id);
    
    // Allocates new page slot block identifier on disk and loads it
    Page* NewPage(page_id_t* page_id);
    
    // Removes page from database pool tracking
    bool DeletePage(page_id_t page_id);
    
    // flushes all tracked buffer pool pages to disk
    void FlushAllPages();

    // Helper method to check pool state during tests
    size_t GetPoolSize() const { return pool_size_; }
    DiskManager* GetDiskManager() { return disk_manager_; }

private:
    bool FindAvailableFrame(frame_id_t* frame_id);

    size_t pool_size_;
    DiskManager* disk_manager_;
    ClockReplacer replacer_;
    Page* pages_;
    std::unordered_map<page_id_t, frame_id_t> page_table_;
    std::mutex latch_;
};

} // namespace minidb

#endif // BUFFER_POOL_MANAGER_H
