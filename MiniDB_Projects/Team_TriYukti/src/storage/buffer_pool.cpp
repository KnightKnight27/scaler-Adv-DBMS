#include "storage/buffer_pool.h"

namespace minidb {

BufferPool::BufferPool(size_t pool_size, PageManager *page_manager)
    : pool_size_(pool_size), page_manager_(page_manager) {
    frames_.resize(pool_size_);
    for (size_t i = 0; i < pool_size_; ++i) {
        replacer_.push_back(i);
    }
}

BufferPool::~BufferPool() {
    FlushAllPages();
}

bool BufferPool::FindVictim(size_t *frame_id) {
    for (auto it = replacer_.begin(); it != replacer_.end(); ++it) {
        size_t fid = *it;
        if (frames_[fid].pin_count == 0) {
            *frame_id = fid;
            replacer_.erase(it);
            return true;
        }
    }
    return false;
}

Page* BufferPool::FetchPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    if (page_table_.find(page_id) != page_table_.end()) {
        size_t frame_id = page_table_[page_id];
        frames_[frame_id].pin_count++;
        replacer_.remove(frame_id);
        return &frames_[frame_id].page;
    }

    size_t victim_frame_id;
    if (!FindVictim(&victim_frame_id)) {
        return nullptr; 
    }

    if (frames_[victim_frame_id].is_dirty) {
        page_manager_->WritePage(frames_[victim_frame_id].page_id, &frames_[victim_frame_id].page);
    }

    if (frames_[victim_frame_id].page_id != INVALID_PAGE_ID) {
        page_table_.erase(frames_[victim_frame_id].page_id);
    }
    
    page_table_[page_id] = victim_frame_id;
    frames_[victim_frame_id].page_id = page_id;
    frames_[victim_frame_id].pin_count = 1;
    frames_[victim_frame_id].is_dirty = false;
    
    page_manager_->ReadPage(page_id, &frames_[victim_frame_id].page);
    return &frames_[victim_frame_id].page;
}

Page* BufferPool::NewPage(page_id_t *page_id, page_id_t prev_page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    
    size_t victim_frame_id;
    if (!FindVictim(&victim_frame_id)) {
        return nullptr;
    }

    if (frames_[victim_frame_id].is_dirty && frames_[victim_frame_id].page_id != INVALID_PAGE_ID) {
        page_manager_->WritePage(frames_[victim_frame_id].page_id, &frames_[victim_frame_id].page);
    }

    page_id_t new_page_id = page_manager_->AllocatePage(prev_page_id);
    *page_id = new_page_id;

    if (frames_[victim_frame_id].page_id != INVALID_PAGE_ID) {
        page_table_.erase(frames_[victim_frame_id].page_id);
    }
    
    page_table_[new_page_id] = victim_frame_id;
    frames_[victim_frame_id].page_id = new_page_id;
    frames_[victim_frame_id].pin_count = 1;
    frames_[victim_frame_id].is_dirty = false;
    frames_[victim_frame_id].page.Init(new_page_id);

    return &frames_[victim_frame_id].page;
}

bool BufferPool::UnpinPage(page_id_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);
    
    if (page_table_.find(page_id) == page_table_.end()) {
        return false;
    }

    size_t frame_id = page_table_[page_id];
    if (frames_[frame_id].pin_count <= 0) {
        return false;
    }

    frames_[frame_id].pin_count--;
    if (is_dirty) {
        frames_[frame_id].is_dirty = true;
    }

    if (frames_[frame_id].pin_count == 0) {
        replacer_.push_back(frame_id);
    }

    return true;
}

bool BufferPool::FlushPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (page_table_.find(page_id) == page_table_.end()) {
        return false;
    }
    size_t frame_id = page_table_[page_id];
    page_manager_->WritePage(page_id, &frames_[frame_id].page);
    frames_[frame_id].is_dirty = false;
    return true;
}

void BufferPool::FlushAllPages() {
    std::lock_guard<std::mutex> lock(latch_);
    for (auto const& [page_id, frame_id] : page_table_) {
        if (frames_[frame_id].is_dirty) {
            page_manager_->WritePage(page_id, &frames_[frame_id].page);
            frames_[frame_id].is_dirty = false;
        }
    }
}

} // namespace minidb
