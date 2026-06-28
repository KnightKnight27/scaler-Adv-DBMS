#include "storage/buffer_pool_manager.h"
#include "recovery/log_manager.h"
#include <iostream>

namespace minidb {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
    pages_ = new Page[pool_size_];
    metadata_.resize(pool_size_);
    for (size_t i = 0; i < pool_size_; ++i) {
        metadata_[i].page_id = INVALID_PAGE_ID;
        metadata_[i].pin_count = 0;
        metadata_[i].is_dirty = false;
        metadata_[i].ref_bit = false;
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
    delete[] pages_;
}

Page *BufferPoolManager::FetchPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    if (page_id == INVALID_PAGE_ID || (disk_manager_ && page_id >= disk_manager_->GetNumPages())) {
        return nullptr;
    }

    // 1. Check if page is already in the buffer pool
    auto iter = page_table_.find(page_id);
    if (iter != page_table_.end()) {
        frame_id_t frame_id = iter->second;
        metadata_[frame_id].pin_count++;
        metadata_[frame_id].ref_bit = true;
        return &pages_[frame_id];
    }

    // 2. Page is not in buffer pool. Find a victim frame.
    frame_id_t victim_frame = -1;
    if (!EvictPage(&victim_frame)) {
        return nullptr; // No page can be evicted (all pages pinned)
    }

    // 3. Evict the old page if it is valid
    page_id_t evicted_page_id = metadata_[victim_frame].page_id;
    if (evicted_page_id != INVALID_PAGE_ID) {
        if (metadata_[victim_frame].is_dirty) {
            // WAL restriction: write logs up to the page LSN before writing the page to disk
            lsn_t page_lsn = pages_[victim_frame].GetLSN();
            if (log_manager_ && page_lsn != INVALID_LSN) {
                log_manager_->Flush(page_lsn);
            }
            disk_manager_->WritePage(evicted_page_id, pages_[victim_frame].GetData());
            metadata_[victim_frame].is_dirty = false;
        }
        page_table_.erase(evicted_page_id);
    }

    // 4. Read new page from disk
    pages_[victim_frame].ResetMemory();
    disk_manager_->ReadPage(page_id, pages_[victim_frame].GetData());
    pages_[victim_frame].SetPageId(page_id);

    // 5. Update metadata and page table
    metadata_[victim_frame].page_id = page_id;
    metadata_[victim_frame].pin_count = 1;
    metadata_[victim_frame].is_dirty = false;
    metadata_[victim_frame].ref_bit = true;
    page_table_[page_id] = victim_frame;

    return &pages_[victim_frame];
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);

    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        return false;
    }

    frame_id_t frame_id = iter->second;
    if (metadata_[frame_id].pin_count <= 0) {
        return false;
    }

    metadata_[frame_id].pin_count--;
    if (is_dirty) {
        metadata_[frame_id].is_dirty = true;
    }
    return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        return false;
    }

    frame_id_t frame_id = iter->second;
    if (metadata_[frame_id].is_dirty) {
        lsn_t page_lsn = pages_[frame_id].GetLSN();
        if (log_manager_ && page_lsn != INVALID_LSN) {
            log_manager_->Flush(page_lsn);
        }
        disk_manager_->WritePage(page_id, pages_[frame_id].GetData());
        metadata_[frame_id].is_dirty = false;
    }
    return true;
}

Page *BufferPoolManager::NewPage() {
    std::lock_guard<std::mutex> lock(latch_);

    // 1. Find a victim frame
    frame_id_t victim_frame = -1;
    if (!EvictPage(&victim_frame)) {
        return nullptr;
    }

    // 2. Allocate page ID
    page_id_t new_page_id = disk_manager_->AllocatePage();

    // 3. Evict old page if it exists
    page_id_t evicted_page_id = metadata_[victim_frame].page_id;
    if (evicted_page_id != INVALID_PAGE_ID) {
        if (metadata_[victim_frame].is_dirty) {
            lsn_t page_lsn = pages_[victim_frame].GetLSN();
            if (log_manager_ && page_lsn != INVALID_LSN) {
                log_manager_->Flush(page_lsn);
            }
            disk_manager_->WritePage(evicted_page_id, pages_[victim_frame].GetData());
            metadata_[victim_frame].is_dirty = false;
        }
        page_table_.erase(evicted_page_id);
    }

    // 4. Initialize new page in buffer pool
    pages_[victim_frame].ResetMemory();
    pages_[victim_frame].SetPageId(new_page_id);

    // Initialize slotted page structure on it
    SlottedPage slotted(&pages_[victim_frame]);
    slotted.Init(new_page_id);

    // 5. Update metadata and page table
    metadata_[victim_frame].page_id = new_page_id;
    metadata_[victim_frame].pin_count = 1;
    metadata_[victim_frame].is_dirty = true; // since it's newly initialized
    metadata_[victim_frame].ref_bit = true;
    page_table_[new_page_id] = victim_frame;

    return &pages_[victim_frame];
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    auto iter = page_table_.find(page_id);
    if (iter != page_table_.end()) {
        frame_id_t frame_id = iter->second;
        if (metadata_[frame_id].pin_count > 0) {
            return false; // page pinned, cannot delete
        }
        page_table_.erase(page_id);
        metadata_[frame_id].page_id = INVALID_PAGE_ID;
        metadata_[frame_id].is_dirty = false;
        metadata_[frame_id].pin_count = 0;
        metadata_[frame_id].ref_bit = false;
        pages_[frame_id].ResetMemory();
    }
    // Note: in a full implementation we would add it to a free page list on disk,
    // but for our MiniDB, simple delete in buffer pool is fine.
    return true;
}

void BufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> lock(latch_);
    for (size_t i = 0; i < pool_size_; ++i) {
        if (metadata_[i].page_id != INVALID_PAGE_ID && metadata_[i].is_dirty) {
            lsn_t page_lsn = pages_[i].GetLSN();
            if (log_manager_ && page_lsn != INVALID_LSN) {
                log_manager_->Flush(page_lsn);
            }
            disk_manager_->WritePage(metadata_[i].page_id, pages_[i].GetData());
            metadata_[i].is_dirty = false;
        }
    }
}

bool BufferPoolManager::EvictPage(frame_id_t *frame_id) {
    // Check first if there are any unallocated slots (page_id == INVALID_PAGE_ID)
    for (size_t i = 0; i < pool_size_; ++i) {
        if (metadata_[i].page_id == INVALID_PAGE_ID) {
            *frame_id = static_cast<frame_id_t>(i);
            return true;
        }
    }

    // Clock sweep algorithm
    size_t scans = 0;
    while (scans < 2 * pool_size_) {
        frame_id_t curr = clock_hand_;
        clock_hand_ = (clock_hand_ + 1) % pool_size_;

        if (metadata_[curr].pin_count == 0) {
            if (metadata_[curr].ref_bit) {
                metadata_[curr].ref_bit = false;
            } else {
                *frame_id = curr;
                return true;
            }
        }
        scans++;
    }
    return false;
}

} // namespace minidb
