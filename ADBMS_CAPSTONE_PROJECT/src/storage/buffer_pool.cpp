#include "storage/buffer_pool.h"
#include <iostream>

namespace minidb {

BufferPool::BufferPool(size_t pool_size, DiskManager* disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), pages_(pool_size) {
    
    // Initially all frames are free, put them in LRU list
    for (size_t i = 0; i < pool_size_; ++i) {
        lru_list_.push_back(i);
    }
}

BufferPool::~BufferPool() {
    FlushAllPages();
}

bool BufferPool::FindFreeFrame(int* frame_id) {
    if (lru_list_.empty()) {
        return false; // All pages pinned!
    }
    
    // Pop least recently used frame
    *frame_id = lru_list_.front();
    lru_list_.pop_front();
    
    // If the old page in this frame is dirty, flush it to disk first
    Page* old_page = &pages_[*frame_id];
    if (old_page->page_id_ != INVALID_PAGE_ID) {
        if (old_page->is_dirty_) {
            disk_manager_->WritePage(old_page->page_id_, old_page->data_);
        }
        page_table_.erase(old_page->page_id_);
    }
    
    return true;
}

Page* BufferPool::FetchPage(PageId page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    
    // If page is already in the pool
    if (page_table_.find(page_id) != page_table_.end()) {
        int frame_id = page_table_[page_id];
        Page* page = &pages_[frame_id];
        
        // Remove from LRU list if it was unpinned, as it's now pinned again
        if (page->pin_count_ == 0) {
            lru_list_.remove(frame_id);
        }
        page->pin_count_++;
        return page;
    }
    
    // Page is not in the pool, need to bring it from disk
    int frame_id = -1;
    if (!FindFreeFrame(&frame_id)) {
        return nullptr; // Pool is full and all pages are pinned
    }
    
    Page* page = &pages_[frame_id];
    page->ResetMemory();
    page->page_id_ = page_id;
    page->pin_count_ = 1;
    page->is_dirty_ = false;
    
    // Read from disk
    disk_manager_->ReadPage(page_id, page->data_);
    
    // Add to page table
    page_table_[page_id] = frame_id;
    
    return page;
}

bool BufferPool::UnpinPage(PageId page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);
    
    if (page_table_.find(page_id) == page_table_.end()) {
        return false; // Page not found
    }
    
    int frame_id = page_table_[page_id];
    Page* page = &pages_[frame_id];
    
    if (page->pin_count_ <= 0) {
        return false; // Already completely unpinned
    }
    
    page->pin_count_--;
    if (is_dirty) {
        page->is_dirty_ = true;
    }
    
    // If pin count reaches 0, add to LRU (at the back as most recently used)
    if (page->pin_count_ == 0) {
        lru_list_.push_back(frame_id);
    }
    
    return true;
}

bool BufferPool::FlushPage(PageId page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    
    if (page_table_.find(page_id) == page_table_.end()) {
        return false;
    }
    
    int frame_id = page_table_[page_id];
    Page* page = &pages_[frame_id];
    
    disk_manager_->WritePage(page->page_id_, page->data_);
    page->is_dirty_ = false;
    std::cout << "[Diagnostic] Page flush: " << page->page_id_ << "\n";
    
    return true;
}

void BufferPool::FlushAllPages() {
    std::lock_guard<std::mutex> lock(latch_);
    for (size_t i = 0; i < pool_size_; ++i) {
        Page* page = &pages_[i];
        if (page->page_id_ != INVALID_PAGE_ID && page->is_dirty_) {
            disk_manager_->WritePage(page->page_id_, page->data_);
            page->is_dirty_ = false;
            std::cout << "[Diagnostic] Page flush: " << page->page_id_ << "\n";
        }
    }
}

Page* BufferPool::NewPage(PageId* page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    
    int frame_id = -1;
    if (!FindFreeFrame(&frame_id)) {
        return nullptr;
    }
    
    *page_id = disk_manager_->AllocatePage();
    
    Page* page = &pages_[frame_id];
    page->ResetMemory();
    page->page_id_ = *page_id;
    page->pin_count_ = 1;
    page->is_dirty_ = true; // Mark dirty since it needs to be written to disk eventually
    
    page_table_[*page_id] = frame_id;
    
    return page;
}

} // namespace minidb
