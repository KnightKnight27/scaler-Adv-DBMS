#include "storage/buffer_pool.h"
#include <cassert>
#include <cstring>

namespace minidb {

BufferPoolManager::BufferPoolManager(uint32_t pool_size, DiskManager* disk_mgr)
    : pool_size_(pool_size), disk_mgr_(disk_mgr) {
    frames_.resize(pool_size);
    for (uint32_t i = 0; i < pool_size; i++) {
        free_list_.push_back(i);
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAll();
}

Page* BufferPoolManager::FetchPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    // 1. Check if page is already in buffer pool
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        uint32_t frame_id = it->second;
        auto& frame = frames_[frame_id];
        frame.pin_count++;
        // Remove from LRU if it was there (it's pinned now)
        LRURemove(frame_id);
        return &frame.page;
    }

    // 2. Need to bring page from disk — find a free frame or evict
    int frame_id = -1;
    if (!free_list_.empty()) {
        frame_id = static_cast<int>(free_list_.front());
        free_list_.pop_front();
    } else {
        frame_id = EvictFrame();
        if (frame_id == -1) return nullptr; // all pinned, can't evict
    }

    // 3. Read page from disk
    auto& frame = frames_[frame_id];
    disk_mgr_->ReadPage(page_id, frame.page.GetMutableData());
    frame.page.DeserializeHeader();
    frame.page.SetPageId(page_id);
    frame.page_id   = page_id;
    frame.pin_count  = 1;
    frame.is_dirty   = false;

    page_table_[page_id] = frame_id;
    return &frame.page;
}

Page* BufferPoolManager::NewPage(page_id_t& new_page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    // Find a free frame or evict
    int frame_id = -1;
    if (!free_list_.empty()) {
        frame_id = static_cast<int>(free_list_.front());
        free_list_.pop_front();
    } else {
        frame_id = EvictFrame();
        if (frame_id == -1) return nullptr;
    }

    // Allocate a new page on disk
    new_page_id = disk_mgr_->AllocatePage();

    auto& frame = frames_[frame_id];
    frame.page.Init(new_page_id);
    frame.page_id   = new_page_id;
    frame.pin_count  = 1;
    frame.is_dirty   = true;

    page_table_[new_page_id] = frame_id;
    return &frame.page;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);

    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;

    uint32_t frame_id = it->second;
    auto& frame = frames_[frame_id];
    if (frame.pin_count == 0) return false;

    frame.is_dirty = frame.is_dirty || is_dirty;
    frame.pin_count--;

    if (frame.pin_count == 0) {
        // Page is now unpinned — add to LRU
        LRUInsert(frame_id);
    }
    return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;

    uint32_t frame_id = it->second;
    auto& frame = frames_[frame_id];
    frame.page.SerializeHeader();
    disk_mgr_->WritePage(page_id, frame.page.GetRawData());
    frame.is_dirty = false;
    return true;
}

void BufferPoolManager::FlushAll() {
    // Note: we don't hold the lock here because FlushPage acquires it.
    // Collect page_ids first.
    std::vector<page_id_t> pages;
    {
        std::lock_guard<std::mutex> lock(latch_);
        for (auto& [pid, fid] : page_table_) {
            pages.push_back(pid);
        }
    }
    for (auto pid : pages) {
        FlushPage(pid);
    }
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return true; // not in pool, fine

    uint32_t frame_id = it->second;
    auto& frame = frames_[frame_id];
    if (frame.pin_count > 0) return false; // can't delete pinned page

    LRURemove(frame_id);
    page_table_.erase(it);
    frame.page_id  = INVALID_PAGE_ID;
    frame.is_dirty = false;
    free_list_.push_back(frame_id);
    return true;
}

int BufferPoolManager::EvictFrame() {
    // Evict from back of LRU (least recently used)
    if (lru_list_.empty()) return -1;

    uint32_t frame_id = lru_list_.back();
    lru_list_.pop_back();
    lru_map_.erase(frame_id);

    auto& frame = frames_[frame_id];
    if (frame.is_dirty) {
        frame.page.SerializeHeader();
        disk_mgr_->WritePage(frame.page_id, frame.page.GetRawData());
    }
    page_table_.erase(frame.page_id);
    frame.page_id = INVALID_PAGE_ID;
    frame.is_dirty = false;
    return static_cast<int>(frame_id);
}

void BufferPoolManager::LRURemove(uint32_t frame_id) {
    auto it = lru_map_.find(frame_id);
    if (it != lru_map_.end()) {
        lru_list_.erase(it->second);
        lru_map_.erase(it);
    }
}

void BufferPoolManager::LRUInsert(uint32_t frame_id) {
    // Insert at front (most recently used)
    lru_list_.push_front(frame_id);
    lru_map_[frame_id] = lru_list_.begin();
}

} // namespace minidb
