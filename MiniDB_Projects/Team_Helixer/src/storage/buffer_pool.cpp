#include "storage/buffer_pool.h"
#include <cstring>

namespace minidb {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk)
    : pool_size_(pool_size), disk_(disk) {
    frames_ = new Page[pool_size_];
    // Initially every frame is free.
    for (size_t i = 0; i < pool_size_; ++i) {
        free_list_.push_back(static_cast<frame_id_t>(i));
    }
}

BufferPoolManager::~BufferPoolManager() {
    flush_all();
    delete[] frames_;
}

bool BufferPoolManager::find_victim_frame(frame_id_t *frame_id) {
    // Prefer a truly free frame; only evict when none are free.
    if (!free_list_.empty()) {
        *frame_id = free_list_.front();
        free_list_.pop_front();
        return true;
    }
    if (!replacer_.victim(frame_id)) return false; // all frames pinned

    // Evicting: write back if dirty and drop the old mapping.
    Page &victim = frames_[*frame_id];
    if (victim.is_dirty()) {
        disk_->write_page(victim.page_id(), victim.data());
    }
    page_table_.erase(victim.page_id());
    victim.reset();
    return true;
}

Page *BufferPoolManager::new_page(page_id_t *page_id) {
    std::lock_guard<std::mutex> guard(latch_);
    frame_id_t fid;
    if (!find_victim_frame(&fid)) return nullptr;

    page_id_t new_id = disk_->allocate_page();
    Page &frame = frames_[fid];
    frame.reset();
    frame.set_page_id(new_id);
    frame.pin();
    replacer_.pin(fid);
    page_table_[new_id] = fid;

    // Materialise the empty page on disk so its slot exists.
    disk_->write_page(new_id, frame.data());
    *page_id = new_id;
    return &frame;
}

Page *BufferPoolManager::fetch_page(page_id_t page_id) {
    std::lock_guard<std::mutex> guard(latch_);
    // Cache hit: bump pin count and remove from eviction candidates.
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        Page &frame = frames_[it->second];
        frame.pin();
        replacer_.pin(it->second);
        return &frame;
    }
    // Cache miss: bring it in from disk.
    frame_id_t fid;
    if (!find_victim_frame(&fid)) return nullptr;
    Page &frame = frames_[fid];
    frame.reset();
    frame.set_page_id(page_id);
    frame.pin();
    replacer_.pin(fid);
    disk_->read_page(page_id, frame.data());
    page_table_[page_id] = fid;
    return &frame;
}

bool BufferPoolManager::unpin_page(page_id_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> guard(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;
    Page &frame = frames_[it->second];
    if (frame.pin_count() <= 0) return false;
    if (is_dirty) frame.set_dirty(true);
    frame.unpin();
    // Once nobody is using the frame it may be evicted.
    if (frame.pin_count() == 0) replacer_.unpin(it->second);
    return true;
}

bool BufferPoolManager::flush_page(page_id_t page_id) {
    std::lock_guard<std::mutex> guard(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;
    Page &frame = frames_[it->second];
    disk_->write_page(page_id, frame.data());
    frame.set_dirty(false);
    return true;
}

void BufferPoolManager::flush_all() {
    std::lock_guard<std::mutex> guard(latch_);
    for (auto &kv : page_table_) {
        Page &frame = frames_[kv.second];
        if (frame.is_dirty()) {
            disk_->write_page(frame.page_id(), frame.data());
            frame.set_dirty(false);
        }
    }
}

} // namespace minidb
