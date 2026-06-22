#include "storage/buffer_pool_manager.h"

namespace minidb {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager* disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), replacer_(pool_size) {
    pages_ = new Page[pool_size_];
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
    delete[] pages_;
}

bool BufferPoolManager::FindAvailableFrame(frame_id_t* frame_id) {
    // 1. Look for an empty frame in the pool
    for (size_t i = 0; i < pool_size_; ++i) {
        if (pages_[i].GetPageId() == INVALID_PAGE_ID) {
            *frame_id = static_cast<frame_id_t>(i);
            return true;
        }
    }
    // 2. Try to get a victim from the replacer
    return replacer_.Victim(frame_id);
}

Page* BufferPoolManager::FetchPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    // 1. Search page_table_
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        frame_id_t frame_id = it->second;
        Page* page = &pages_[frame_id];
        page->pin_count_++;
        replacer_.Pin(frame_id);
        return page;
    }

    // 2. Find available frame
    frame_id_t frame_id = INVALID_FRAME_ID;
    if (!FindAvailableFrame(&frame_id)) {
        return nullptr;
    }

    Page* page = &pages_[frame_id];

    // 3. If victim frame contains a dirty page, write it to disk
    if (page->GetPageId() != INVALID_PAGE_ID) {
        if (page->IsDirty()) {
            disk_manager_->WritePage(page->GetPageId(), page->GetData());
        }
        page_table_.erase(page->GetPageId());
    }

    // 4. Read page from disk into frame
    page->ResetMemory();
    disk_manager_->ReadPage(page_id, page->GetData());
    page->page_id_ = page_id;
    page->pin_count_ = 1;
    page->is_dirty_ = false;

    page_table_[page_id] = frame_id;
    replacer_.Pin(frame_id);

    return page;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);

    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }

    frame_id_t frame_id = it->second;
    Page* page = &pages_[frame_id];

    if (is_dirty) {
        page->is_dirty_ = true;
    }

    if (page->GetPinCount() <= 0) {
        return false;
    }

    page->pin_count_--;
    if (page->GetPinCount() == 0) {
        replacer_.Unpin(frame_id);
    }
    return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }

    frame_id_t frame_id = it->second;
    Page* page = &pages_[frame_id];

    if (page->IsDirty()) {
        disk_manager_->WritePage(page->GetPageId(), page->GetData());
        page->is_dirty_ = false;
    }
    return true;
}

Page* BufferPoolManager::NewPage(page_id_t* page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    // Find available frame
    frame_id_t frame_id = INVALID_FRAME_ID;
    if (!FindAvailableFrame(&frame_id)) {
        return nullptr;
    }

    // Allocate on disk
    *page_id = disk_manager_->AllocatePage();

    Page* page = &pages_[frame_id];

    // If victim page was dirty, write to disk
    if (page->GetPageId() != INVALID_PAGE_ID) {
        if (page->IsDirty()) {
            disk_manager_->WritePage(page->GetPageId(), page->GetData());
        }
        page_table_.erase(page->GetPageId());
    }

    // Reset frame and set new page metadata
    page->ResetMemory();
    page->page_id_ = *page_id;
    page->pin_count_ = 1;
    page->is_dirty_ = false;

    page_table_[*page_id] = frame_id;
    replacer_.Pin(frame_id);

    return page;
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return true; // Already not in buffer pool
    }

    frame_id_t frame_id = it->second;
    Page* page = &pages_[frame_id];

    if (page->GetPinCount() > 0) {
        return false; // Cannot delete a pinned page
    }

    // If dirty, flush to disk first
    if (page->IsDirty()) {
        disk_manager_->WritePage(page->GetPageId(), page->GetData());
    }

    page_table_.erase(page_id);
    replacer_.Pin(frame_id); // Remove from replacer candidate list
    page->ResetMemory();

    return true;
}

void BufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> lock(latch_);
    for (size_t i = 0; i < pool_size_; ++i) {
        Page* page = &pages_[i];
        if (page->GetPageId() != INVALID_PAGE_ID && page->IsDirty()) {
            disk_manager_->WritePage(page->GetPageId(), page->GetData());
            page->is_dirty_ = false;
        }
    }
}

} // namespace minidb
