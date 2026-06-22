#include "buffer_pool.h"

#include <stdexcept>

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), pages_(std::make_unique<Page[]>(pool_size)) {
    for (size_t i = 0; i < pool_size_; ++i) {
        free_list_.push_back(i);
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
}

void BufferPoolManager::TouchFrame(size_t frame_id) {
    auto it = lru_map_.find(frame_id);
    if (it != lru_map_.end()) {
        lru_list_.erase(it->second);
    }
    lru_list_.push_front(frame_id);
    lru_map_[frame_id] = lru_list_.begin();
}

size_t BufferPoolManager::Evict() {
    // Walk LRU from back (least recently used) to find an unpinned frame.
    for (auto it = lru_list_.rbegin(); it != lru_list_.rend(); ++it) {
        size_t frame_id = *it;
        if (pages_[frame_id].pin_count == 0) {
            return frame_id;
        }
    }
    return pool_size_; // no victim found
}

Page *BufferPoolManager::NewPage(page_id_t &page_id) {
    size_t frame_id;

    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
    } else {
        frame_id = Evict();
        if (frame_id == pool_size_) {
            return nullptr; // all frames are pinned
        }
        // Flush victim if dirty.
        Page &victim = pages_[frame_id];
        if (victim.is_dirty) {
            disk_manager_->WritePage(victim.page_id, victim.data);
        }
        page_table_.erase(victim.page_id);
        // Remove from LRU.
        auto it = lru_map_.find(frame_id);
        if (it != lru_map_.end()) {
            lru_list_.erase(it->second);
            lru_map_.erase(it);
        }
    }

    page_id = disk_manager_->AllocatePage();
    Page &page = pages_[frame_id];
    page.Reset();
    page.page_id = page_id;
    page.pin_count = 1;
    page_table_[page_id] = frame_id;
    TouchFrame(frame_id);
    return &page;
}

Page *BufferPoolManager::FetchPage(page_id_t page_id) {
    // Already buffered?
    auto table_it = page_table_.find(page_id);
    if (table_it != page_table_.end()) {
        size_t frame_id = table_it->second;
        pages_[frame_id].pin_count++;
        TouchFrame(frame_id);
        return &pages_[frame_id];
    }

    // Need a frame.
    size_t frame_id;
    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
    } else {
        frame_id = Evict();
        if (frame_id == pool_size_) {
            return nullptr;
        }
        Page &victim = pages_[frame_id];
        if (victim.is_dirty) {
            disk_manager_->WritePage(victim.page_id, victim.data);
        }
        page_table_.erase(victim.page_id);
        auto it = lru_map_.find(frame_id);
        if (it != lru_map_.end()) {
            lru_list_.erase(it->second);
            lru_map_.erase(it);
        }
    }

    Page &page = pages_[frame_id];
    page.Reset();
    page.page_id = page_id;
    page.pin_count = 1;
    disk_manager_->ReadPage(page_id, page.data);
    page_table_[page_id] = frame_id;
    TouchFrame(frame_id);
    return &page;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }
    Page &page = pages_[it->second];
    if (page.pin_count <= 0) {
        return false;
    }
    if (is_dirty) {
        page.is_dirty = true;
    }
    --page.pin_count;
    return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }
    Page &page = pages_[it->second];
    disk_manager_->WritePage(page_id, page.data);
    page.is_dirty = false;
    return true;
}

void BufferPoolManager::FlushAllPages() {
    for (auto &[pid, frame_id] : page_table_) {
        Page &page = pages_[frame_id];
        if (page.is_dirty) {
            disk_manager_->WritePage(pid, page.data);
            page.is_dirty = false;
        }
    }
}

bool BufferPoolManager::AllUnpinned() const {
    for (const auto &[pid, frame_id] : page_table_) {
        if (pages_[frame_id].pin_count > 0) return false;
    }
    return true;
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return true; // not in pool — nothing to do
    }
    size_t frame_id = it->second;
    if (pages_[frame_id].pin_count > 0) {
        return false; // still pinned
    }
    page_table_.erase(it);
    auto lru_it = lru_map_.find(frame_id);
    if (lru_it != lru_map_.end()) {
        lru_list_.erase(lru_it->second);
        lru_map_.erase(lru_it);
    }
    pages_[frame_id].Reset();
    free_list_.push_back(frame_id);
    return true;
}
