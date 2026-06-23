#pragma once

#include <unordered_map>
#include <mutex>
#include <vector>
#include <functional>
#include "storage/disk.h"
#include "storage/page.h"

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager* disk_manager);
    ~BufferPoolManager();

    Page* FetchPage(PageId_t page_id);
    bool UnpinPage(PageId_t page_id, bool is_dirty);
    bool FlushPage(PageId_t page_id);
    Page* NewPage(PageId_t& page_id);
    bool DeletePage(PageId_t page_id);
    void FlushAllPages();

    // Stats
    size_t GetPoolSize() const { return pool_size_; }
    size_t GetCacheHits() const { return cache_hits_; }
    size_t GetCacheMisses() const { return cache_misses_; }
    void ResetStats() { cache_hits_ = 0; cache_misses_ = 0; }

    // Register callback to flush WAL up to a certain LSN
    void RegisterLogFlushCallback(std::function<void(Lsn_t)> callback) {
        log_flush_callback_ = callback;
    }
    void SetLogFlushedLsn(Lsn_t lsn) { log_flushed_lsn_ = lsn; }

private:
    bool FindVictim(size_t& victim_frame_id);
    bool FlushPageInternal(size_t frame_id);

    size_t pool_size_;
    DiskManager* disk_manager_;
    std::vector<Page> pages_; // Page frames
    
    struct FrameMetadata {
        PageId_t page_id = INVALID_PAGE_ID;
        int pin_count = 0;
        bool is_dirty = false;
        bool reference_bit = false;
    };
    std::vector<FrameMetadata> frame_metadata_;
    std::unordered_map<PageId_t, size_t> page_table_;
    size_t clock_hand_ = 0;
    
    std::mutex latch_;
    size_t cache_hits_ = 0;
    size_t cache_misses_ = 0;

    Lsn_t log_flushed_lsn_ = 0;
    std::function<void(Lsn_t)> log_flush_callback_ = nullptr;
};
