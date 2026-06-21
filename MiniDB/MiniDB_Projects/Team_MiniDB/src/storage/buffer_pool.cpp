#include "buffer_pool.hpp"

#include <stdexcept>

#include "disk_manager.hpp"

constexpr std::uint8_t MAX_USAGE = 5;  // usage cap (same as lab 3 / PostgreSQL)

BufferPool::BufferPool(DiskManager& disk, std::size_t num_frames)
    : disk_(disk), frames_(num_frames) {}

char* BufferPool::fetch_page(PageID id) {
    // Hit: page already resident. Pin it and bump its usage ("hotness").
    auto it = table_.find(id);
    if (it != table_.end()) {
        Frame& f = frames_[it->second];
        f.pin_count++;
        if (f.usage < MAX_USAGE) f.usage++;
        return f.data;
    }

    // Miss: choose a frame, flush it if dirty, then load the requested page.
    std::size_t idx = find_victim();
    Frame& f = frames_[idx];
    if (f.valid) {
        if (f.dirty) disk_.write_page(f.page_id, f.data);
        table_.erase(f.page_id);
    }
    disk_.read_page(id, f.data);
    f.page_id = id;
    f.pin_count = 1;
    f.dirty = false;
    f.usage = 1;
    f.valid = true;
    table_[id] = idx;
    return f.data;
}

bool BufferPool::unpin_page(PageID id, bool dirty) {
    auto it = table_.find(id);
    if (it == table_.end()) return false;
    Frame& f = frames_[it->second];
    if (f.pin_count > 0) f.pin_count--;
    if (dirty) f.dirty = true;  // sticky: once dirty, stays dirty until flushed
    return true;
}

PageID BufferPool::new_page() {
    return disk_.allocate_page();
}

void BufferPool::flush_page(PageID id) {
    auto it = table_.find(id);
    if (it == table_.end()) return;
    Frame& f = frames_[it->second];
    if (f.valid && f.dirty) {
        disk_.write_page(f.page_id, f.data);
        f.dirty = false;
    }
}

void BufferPool::flush_all() {
    for (Frame& f : frames_) {
        if (f.valid && f.dirty) {
            disk_.write_page(f.page_id, f.data);
            f.dirty = false;
        }
    }
}

std::size_t BufferPool::find_victim() {
    // Prefer an empty frame if one exists.
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        if (!frames_[i].valid) return i;
    }
    // Otherwise sweep the clock hand: skip pinned frames, decrement usage,
    // and evict the first unpinned frame whose usage has fallen to 0.
    std::size_t scanned = 0;
    std::size_t limit = frames_.size() * (MAX_USAGE + 1);  // bounded by usage cap
    while (scanned++ < limit) {
        Frame& f = frames_[hand_];
        std::size_t cur = hand_;
        hand_ = (hand_ + 1) % frames_.size();
        if (f.pin_count > 0) continue;
        if (f.usage == 0) return cur;
        f.usage--;
    }
    throw std::runtime_error("BufferPool: all frames pinned, cannot evict");
}
