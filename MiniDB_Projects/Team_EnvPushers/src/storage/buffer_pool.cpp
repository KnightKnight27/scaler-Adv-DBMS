#include "storage/buffer_pool.hpp"

#include <stdexcept>

namespace minidb {

void BufferPool::touch(PageId pid) {
    auto& f = frames_[pid];
    lru_.erase(f->lru_it);
    lru_.push_back(pid);
    f->lru_it = std::prev(lru_.end());
}

Page* BufferPool::fetch_page(PageId pid) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = frames_.find(pid);
    if (it != frames_.end()) {
        stats_.hits++;
        it->second->pin_count++;
        touch(pid);
        return &it->second->page;
    }
    stats_.misses++;
    evict_if_needed();

    auto frame = std::make_unique<Frame>();
    frame->page = Page(pid, /*init=*/false);
    disk_->read_page(pid, frame->page.raw());
    frame->pin_count = 1;
    lru_.push_back(pid);
    frame->lru_it = std::prev(lru_.end());
    Page* ptr = &frame->page;
    frames_[pid] = std::move(frame);
    return ptr;
}

Page* BufferPool::new_page() {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    PageId pid = disk_->allocate_page();
    evict_if_needed();

    auto frame = std::make_unique<Frame>();
    frame->page = Page(pid, /*init=*/true);
    frame->pin_count = 1;
    frame->dirty = true;
    lru_.push_back(pid);
    frame->lru_it = std::prev(lru_.end());
    Page* ptr = &frame->page;
    frames_[pid] = std::move(frame);
    return ptr;
}

void BufferPool::unpin_page(PageId pid, bool is_dirty) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = frames_.find(pid);
    if (it == frames_.end()) return;
    if (is_dirty) it->second->dirty = true;
    if (it->second->pin_count > 0) it->second->pin_count--;
}

void BufferPool::flush_frame(Frame& f) {
    if (!f.dirty) return;
    if (before_flush_) before_flush_(f.page.page_id());  // WAL: log first
    disk_->write_page(f.page.page_id(), f.page.raw());
    f.dirty = false;
}

void BufferPool::flush_page(PageId pid) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = frames_.find(pid);
    if (it != frames_.end()) flush_frame(*it->second);
}

void BufferPool::flush_all() {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    for (auto& [pid, frame] : frames_) flush_frame(*frame);
}

void BufferPool::evict_if_needed() {
    if (frames_.size() < pool_size_) return;
    // Walk LRU order (front = least recent) for the first unpinned frame.
    for (auto it = lru_.begin(); it != lru_.end(); ++it) {
        PageId pid = *it;
        auto fit = frames_.find(pid);
        if (fit->second->pin_count == 0) {
            flush_frame(*fit->second);
            lru_.erase(it);
            frames_.erase(fit);
            stats_.evictions++;
            return;
        }
    }
    throw std::runtime_error("BufferPool full: every frame is pinned");
}

}  // namespace minidb
