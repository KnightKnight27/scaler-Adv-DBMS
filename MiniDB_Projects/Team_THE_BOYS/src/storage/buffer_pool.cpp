#include "storage/buffer_pool.h"

#include <algorithm>
#include <stdexcept>

namespace minidb {

BufferPool::BufferPool(PageManager* page_manager, std::size_t pool_size)
    : page_manager_(page_manager), pool_size_(pool_size) {
    frames_.resize(pool_size_);
    for (std::size_t i = 0; i < pool_size_; ++i) {
        frames_[i].page_id = INVALID_PAGE_ID;
        lru_.push_back(i);
    }
}

Page* BufferPool::FetchPage(int page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        hits_++;
        Touch(it->second);
        frames_[it->second].page.set_pin_count(frames_[it->second].page.pin_count() + 1);
        return &frames_[it->second].page;
    }

    misses_++;
    std::size_t frame_idx = EvictFrame();
    Frame& frame = frames_[frame_idx];
    if (frame.page_id != INVALID_PAGE_ID) {
        if (frame.dirty) {
            page_manager_->FlushPage(frame.page_id, frame.page);
        }
        page_table_.erase(frame.page_id);
    }

    Page* loaded = page_manager_->FetchPage(page_id);
    frame.page = *loaded;
    delete loaded;
    frame.page_id = page_id;
    frame.dirty = false;
    frame.page.set_pin_count(1);
    page_table_[page_id] = frame_idx;
    Touch(frame_idx);
    return &frame.page;
}

void BufferPool::UnpinPage(int page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return;
    }
    Frame& frame = frames_[it->second];
    if (is_dirty) {
        frame.dirty = true;
        frame.page.set_dirty(true);
    }
    int pins = frame.page.pin_count() - 1;
    frame.page.set_pin_count(std::max(0, pins));
}

void BufferPool::FlushAllPages() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& frame : frames_) {
        if (frame.page_id != INVALID_PAGE_ID && frame.dirty) {
            page_manager_->FlushPage(frame.page_id, frame.page);
            frame.dirty = false;
            frame.page.set_dirty(false);
        }
    }
    page_manager_->FlushAll();
}

void BufferPool::ResetCounters() {
    std::lock_guard<std::mutex> lock(mutex_);
    hits_ = 0;
    misses_ = 0;
}

std::size_t BufferPool::EvictFrame() {
    for (auto lit = lru_.rbegin(); lit != lru_.rend(); ++lit) {
        Frame& frame = frames_[*lit];
        if (frame.page.pin_count() == 0) {
            std::size_t idx = *lit;
            lru_.remove(idx);
            lru_.push_front(idx);
            return idx;
        }
    }
    throw std::runtime_error("Buffer pool full: no evictable frame");
}

void BufferPool::Touch(std::size_t frame_idx) {
    lru_.remove(frame_idx);
    lru_.push_front(frame_idx);
}

}  // namespace minidb
