#include "storage/buffer_pool.h"

#include <algorithm>

#include "common/config.h"
#include "common/exception.h"

namespace minidb {

BufferPool::BufferPool(std::size_t capacity, DiskManager* disk)
    : capacity_(capacity), disk_(disk), frames_(capacity) {
    free_list_.reserve(capacity);
    for (int i = static_cast<int>(capacity) - 1; i >= 0; --i) free_list_.push_back(i);
}

Page* BufferPool::fetch_page(PageId pid) {
    std::lock_guard<std::mutex> guard(mutex_);

    auto it = page_table_.find(pid);
    if (it != page_table_.end()) {
        Frame& f = frames_[it->second];
        ++f.pin_count;
        f.usage_count = std::min(f.usage_count + 1, MAX_USAGE_COUNT);
        ++hits_;
        return &f.page;
    }

    ++misses_;
    int idx = grab_frame();
    Frame& f = frames_[idx];

    disk_->read_page(pid, f.page.data());
    f.page.set_page_id(pid);
    f.pin_count   = 1;
    f.dirty       = false;
    f.usage_count = 1;
    page_table_[pid] = idx;
    return &f.page;
}

Page* BufferPool::new_page(PageId& out_pid) {
    std::lock_guard<std::mutex> guard(mutex_);

    PageId pid = disk_->allocate_page();
    int idx = grab_frame();
    Frame& f = frames_[idx];

    f.page.reset();
    f.page.set_page_id(pid);
    f.pin_count   = 1;
    f.dirty       = true;   // ensure the caller's initialisation is persisted
    f.usage_count = 1;
    page_table_[pid] = idx;
    out_pid = pid;
    return &f.page;
}

void BufferPool::unpin_page(PageId pid, bool dirty) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = page_table_.find(pid);
    if (it == page_table_.end()) return;
    Frame& f = frames_[it->second];
    if (f.pin_count > 0) --f.pin_count;
    if (dirty) f.dirty = true;
}

bool BufferPool::flush_page(PageId pid) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = page_table_.find(pid);
    if (it == page_table_.end()) return false;
    flush_frame_locked(it->second);
    return true;
}

void BufferPool::flush_all() {
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& [pid, idx] : page_table_) flush_frame_locked(idx);
}

void BufferPool::flush_frame_locked(int idx) {
    Frame& f = frames_[idx];
    if (f.dirty && f.page.page_id() != INVALID_PAGE_ID) {
        disk_->write_page(f.page.page_id(), f.page.data());
        f.dirty = false;
    }
}

int BufferPool::grab_frame() {
    if (!free_list_.empty()) {
        int idx = free_list_.back();
        free_list_.pop_back();
        return idx;
    }
    int victim = clock_sweep();
    Frame& f = frames_[victim];
    flush_frame_locked(victim);                 // STEAL: write back if dirty
    page_table_.erase(f.page.page_id());
    ++evictions_;
    return victim;
}

int BufferPool::clock_sweep() {
    // A frame's usage_count can be as high as MAX_USAGE_COUNT, and each rotation
    // decrements an unpinned frame by 1, so it can take up to MAX_USAGE_COUNT+1
    // full rotations to drive some unpinned frame to 0 and evict it. We only fail
    // (all frames pinned) if no unpinned frame is found across that many rotations.
    std::size_t max_iters = (static_cast<std::size_t>(MAX_USAGE_COUNT) + 1) * capacity_ + 1;
    for (std::size_t checked = 0; checked < max_iters; ++checked) {
        Frame& f = frames_[hand_];
        if (f.pin_count == 0) {
            if (f.usage_count == 0) {
                int victim = hand_;
                hand_ = (hand_ + 1) % static_cast<int>(capacity_);
                return victim;
            }
            --f.usage_count;
        }
        hand_ = (hand_ + 1) % static_cast<int>(capacity_);
    }
    throw DBException("BufferPool: all frames pinned, cannot evict");
}

} // namespace minidb
