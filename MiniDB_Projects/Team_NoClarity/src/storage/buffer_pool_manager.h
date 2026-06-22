#ifndef BUFFER_POOL_MANAGER_H
#define BUFFER_POOL_MANAGER_H

#include "common/config.h"
#include "storage/page.h"
#include "storage/disk_manager.h"
#include "storage/clock_replacer.h"
#include <unordered_map>
#include <mutex>

namespace minidb {

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager* disk_manager);
    ~BufferPoolManager();

    Page* FetchPage(page_id_t page_id);
    bool UnpinPage(page_id_t page_id, bool is_dirty);
    bool FlushPage(page_id_t page_id);
    Page* NewPage(page_id_t* page_id);
    bool DeletePage(page_id_t page_id);
    void FlushAllPages();

    // Helper method to check pool state during tests
    size_t GetPoolSize() const { return pool_size_; }

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
