#include "storage/buffer.h"
#include <iostream>

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager* disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), pages_(pool_size), frame_metadata_(pool_size) {
    // Initialize frames
    for (size_t i = 0; i < pool_size_; ++i) {
        frame_metadata_[i].page_id = INVALID_PAGE_ID;
        frame_metadata_[i].pin_count = 0;
        frame_metadata_[i].is_dirty = false;
        frame_metadata_[i].reference_bit = false;
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
}

bool BufferPoolManager::FindVictim(size_t& victim_frame_id) {
    size_t scans = 0;
    // We do at most two passes to find a victim
    while (scans < 2 * pool_size_) {
        FrameMetadata& meta = frame_metadata_[clock_hand_];
        if (meta.pin_count == 0) {
            if (meta.reference_bit) {
                meta.reference_bit = false;
            } else {
                victim_frame_id = clock_hand_;
                clock_hand_ = (clock_hand_ + 1) % pool_size_;
                return true;
            }
        }
        clock_hand_ = (clock_hand_ + 1) % pool_size_;
        scans++;
    }
    return false;
}

bool BufferPoolManager::FlushPageInternal(size_t frame_id) {
    FrameMetadata& meta = frame_metadata_[frame_id];
    if (meta.page_id == INVALID_PAGE_ID || !meta.is_dirty) {
        return true;
    }

    Page& page = pages_[frame_id];
    Lsn_t page_lsn = page.GetHeader()->page_lsn;

    // Enforce Write-Ahead Logging (WAL)
    if (page_lsn > log_flushed_lsn_ && log_flush_callback_) {
        log_flush_callback_(page_lsn);
        log_flushed_lsn_ = page_lsn;
    }

    disk_manager_->WritePage(meta.page_id, page.data);
    meta.is_dirty = false;
    return true;
}

Page* BufferPoolManager::FetchPage(PageId_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    // Check if page already in buffer pool
    auto iter = page_table_.find(page_id);
    if (iter != page_table_.end()) {
        size_t frame_id = iter->second;
        frame_metadata_[frame_id].pin_count++;
        frame_metadata_[frame_id].reference_bit = true;
        cache_hits_++;
        return &pages_[frame_id];
    }

    // Cache miss - find a victim to evict
    size_t victim_frame_id = 0;
    if (!FindVictim(victim_frame_id)) {
        cache_misses_++;
        return nullptr; // No frames available to evict
    }

    cache_misses_++;
    FrameMetadata& victim_meta = frame_metadata_[victim_frame_id];

    // Evict old page if valid and dirty
    if (victim_meta.page_id != INVALID_PAGE_ID) {
        if (victim_meta.is_dirty) {
            FlushPageInternal(victim_frame_id);
        }
        page_table_.erase(victim_meta.page_id);
    }

    // Read new page
    disk_manager_->ReadPage(page_id, pages_[victim_frame_id].data);

    // Update metadata
    victim_meta.page_id = page_id;
    victim_meta.pin_count = 1;
    victim_meta.is_dirty = false;
    victim_meta.reference_bit = true;
    page_table_[page_id] = victim_frame_id;

    return &pages_[victim_frame_id];
}

bool BufferPoolManager::UnpinPage(PageId_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);
    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        return false;
    }

    size_t frame_id = iter->second;
    FrameMetadata& meta = frame_metadata_[frame_id];
    if (meta.pin_count <= 0) {
        return false;
    }

    meta.pin_count--;
    if (is_dirty) {
        meta.is_dirty = true;
    }
    return true;
}

bool BufferPoolManager::FlushPage(PageId_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        return false;
    }
    return FlushPageInternal(iter->second);
}

Page* BufferPoolManager::NewPage(PageId_t& page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    // Find victim to evict
    size_t victim_frame_id = 0;
    if (!FindVictim(victim_frame_id)) {
        return nullptr;
    }

    FrameMetadata& victim_meta = frame_metadata_[victim_frame_id];

    // Allocate page ID from disk
    page_id = disk_manager_->AllocatePage();

    // Evict old page if valid and dirty
    if (victim_meta.page_id != INVALID_PAGE_ID) {
        if (victim_meta.is_dirty) {
            FlushPageInternal(victim_frame_id);
        }
        page_table_.erase(victim_meta.page_id);
    }

    // Initialize new page
    pages_[victim_frame_id].Init(page_id, PageType::DATA_PAGE);

    // Setup metadata
    victim_meta.page_id = page_id;
    victim_meta.pin_count = 1;
    victim_meta.is_dirty = true; // Newly allocated page is dirty
    victim_meta.reference_bit = true;
    page_table_[page_id] = victim_frame_id;

    return &pages_[victim_frame_id];
}

bool BufferPoolManager::DeletePage(PageId_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    auto iter = page_table_.find(page_id);
    if (iter != page_table_.end()) {
        size_t frame_id = iter->second;
        if (frame_metadata_[frame_id].pin_count > 0) {
            return false; // Can't delete pinned page
        }
        frame_metadata_[frame_id].page_id = INVALID_PAGE_ID;
        frame_metadata_[frame_id].pin_count = 0;
        frame_metadata_[frame_id].is_dirty = false;
        frame_metadata_[frame_id].reference_bit = false;
        page_table_.erase(page_id);
    }
    disk_manager_->DeallocatePage(page_id);
    return true;
}

void BufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> lock(latch_);
    for (size_t i = 0; i < pool_size_; ++i) {
        if (frame_metadata_[i].page_id != INVALID_PAGE_ID && frame_metadata_[i].is_dirty) {
            FlushPageInternal(i);
        }
    }
}
