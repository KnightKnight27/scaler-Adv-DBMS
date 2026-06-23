#pragma once

#include "storage/disk_manager.h"
#include "storage/page.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace minidb {

class BufferPoolManager {
public:
    BufferPoolManager(DiskManager* disk_manager, std::size_t pool_size);
    ~BufferPoolManager();

    BufferPoolManager(const BufferPoolManager&) = delete;
    BufferPoolManager& operator=(const BufferPoolManager&) = delete;

    char* FetchPage(page_id_t page_id);
    void UnpinPage(page_id_t page_id);
    void MarkDirty(page_id_t page_id);
    void FlushPage(page_id_t page_id);
    void FlushAllPages();

private:
    struct Frame {
        page_id_t page_id = INVALID_PAGE_ID;
        char* data = nullptr;
        uint8_t usage_count = 0;
        int pin_count = 0;
        bool is_dirty = false;
    };

    void FlushAllPagesUnlocked();

    int FindVictimFrame();
    void EvictFrame(int frame_index);

    static constexpr uint8_t kMaxUsageCount = 5;

    DiskManager* disk_manager_;
    std::vector<Frame> frames_;
    std::unordered_map<page_id_t, std::size_t> page_table_;
    std::size_t pool_size_;
    std::size_t clock_hand_;
    std::mutex latch_;
};

}  // namespace minidb
