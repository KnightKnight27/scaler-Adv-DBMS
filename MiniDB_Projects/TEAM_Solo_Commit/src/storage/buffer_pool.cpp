#include "buffer_pool.h"

namespace minidb {

BufferPool::BufferPool(DiskManager* disk, size_t num_frames)
    : disk_(disk), frames_(num_frames) {}

Page* BufferPool::FetchPage(int page_id) {
    std::lock_guard<std::mutex> g(latch_);
    auto it = table_.find(page_id);
    if (it != table_.end()) {
        Frame& f = frames_[it->second];
        f.pin_count++;
        f.ref_bit = true;  // touched -> gets a second chance on the next sweep
        stats_.hits++;
        return &f.page;
    }
    stats_.misses++;
    int idx = FindFrameForLoad();
    if (idx < 0) return nullptr;
    Frame& f = frames_[idx];
    f.page_id = page_id;
    f.dirty = false;
    f.pin_count = 1;
    f.ref_bit = true;
    f.occupied = true;
    disk_->ReadPage(page_id, f.page.data());
    table_[page_id] = idx;
    return &f.page;
}

Page* BufferPool::NewPage(int* out_id) {
    int pid = disk_->AllocatePage();
    Page* p = FetchPage(pid);
    if (p) {
        std::lock_guard<std::mutex> g(latch_);
        frames_[table_[pid]].page.Reset();
        frames_[table_[pid]].dirty = true;
    }
    if (out_id) *out_id = pid;
    return p;
}

bool BufferPool::Unpin(int page_id, bool is_dirty) {
    std::lock_guard<std::mutex> g(latch_);
    auto it = table_.find(page_id);
    if (it == table_.end()) return false;
    Frame& f = frames_[it->second];
    if (f.pin_count > 0) f.pin_count--;
    if (is_dirty) f.dirty = true;
    return true;
}

int BufferPool::FindFrameForLoad() {
    for (size_t i = 0; i < frames_.size(); ++i) {
        if (!frames_[i].occupied) return static_cast<int>(i);
    }
    return ClockEvict();
}

int BufferPool::ClockEvict() {
    // Clock sweep: at most two full passes guarantees a decision when an unpinned
    // frame exists (first pass clears ref bits, second pass evicts).
    size_t n = frames_.size();
    for (size_t scanned = 0; scanned < 2 * n; ++scanned) {
        Frame& f = frames_[hand_];
        size_t cur = hand_;
        hand_ = (hand_ + 1) % n;
        if (f.pin_count > 0) continue;          // in use, skip
        if (f.ref_bit) { f.ref_bit = false; continue; }  // second chance
        // Victim found.
        if (f.dirty) {
            disk_->WritePage(f.page_id, f.page.data());
            f.dirty = false;
        }
        table_.erase(f.page_id);
        f.occupied = false;
        stats_.evictions++;
        return static_cast<int>(cur);
    }
    return -1;  // everything pinned
}

void BufferPool::FlushPage(int page_id) {
    std::lock_guard<std::mutex> g(latch_);
    auto it = table_.find(page_id);
    if (it == table_.end()) return;
    Frame& f = frames_[it->second];
    if (f.dirty) {
        disk_->WritePage(f.page_id, f.page.data());
        f.dirty = false;
    }
}

void BufferPool::FlushAll() {
    std::lock_guard<std::mutex> g(latch_);
    for (Frame& f : frames_) {
        if (f.occupied && f.dirty) {
            disk_->WritePage(f.page_id, f.page.data());
            f.dirty = false;
        }
    }
    disk_->Sync();
}

}  // namespace minidb
