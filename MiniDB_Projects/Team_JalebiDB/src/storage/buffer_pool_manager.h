#pragma once

#include "common/config.h"
#include "common/types.h"
#include "storage/disk_manager.h"
#include "storage/page.h"
#include <unordered_map>
#include <mutex>
#include <vector>

namespace minidb {

class LogManager;

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager *disk_manager, LogManager *log_manager = nullptr);
    ~BufferPoolManager();

    // Fetch the request page from the buffer pool
    Page *FetchPage(page_id_t page_id);

    // Unpin the target page from the buffer pool
    bool UnpinPage(page_id_t page_id, bool is_dirty);

    // Flush the target page to disk
    bool FlushPage(page_id_t page_id);

    // Create a new page in the buffer pool
    Page *NewPage();

    // Delete a page from the buffer pool and disk
    bool DeletePage(page_id_t page_id);

    // Flush all dirty pages to disk
    void FlushAllPages();

    // Helper for debugging/testing
    size_t GetPoolSize() const { return pool_size_; }

private:
    // Evict a page using the clock sweep algorithm
    bool EvictPage(frame_id_t *frame_id);

    size_t pool_size_;
    DiskManager *disk_manager_;
    LogManager *log_manager_;
    Page *pages_; // Array of buffer pool pages
    std::unordered_map<page_id_t, frame_id_t> page_table_;

    // Metadata per frame
    struct FrameMetadata {
        page_id_t page_id{INVALID_PAGE_ID};
        int pin_count{0};
        bool is_dirty{false};
        bool ref_bit{false};
    };
    std::vector<FrameMetadata> metadata_;

    frame_id_t clock_hand_{0};
    std::mutex latch_;
};

} // namespace minidb
