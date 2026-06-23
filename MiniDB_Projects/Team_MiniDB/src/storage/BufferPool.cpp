#include "storage/BufferPool.hpp"

using namespace std;

namespace minidb {

BufferPool::BufferPool(int capacity) : capacity_(capacity) {}

Page* BufferPool::getPage(uint32_t page_id) {
    auto it = frames_.find(page_id);
    if (it == frames_.end()) return nullptr;
    it->second.pin_count++;
    touch(page_id);
    return &it->second.page;
}

void BufferPool::putPage(uint32_t page_id, const Page& page, bool is_dirty) {
    if (frames_.find(page_id) == frames_.end()) {
        evictIfNeeded();
        frames_[page_id] = Frame{page, is_dirty, 1};
        lru_list_.push_front(page_id);
    } else {
        frames_[page_id].page = page;
        frames_[page_id].dirty = frames_[page_id].dirty || is_dirty;
        frames_[page_id].pin_count++;
        touch(page_id);
    }
}

void BufferPool::unpinPage(uint32_t page_id) {
    auto it = frames_.find(page_id);
    if (it != frames_.end() && it->second.pin_count > 0) it->second.pin_count--;
}

void BufferPool::markDirty(uint32_t page_id) {
    auto it = frames_.find(page_id);
    if (it != frames_.end()) it->second.dirty = true;
}

void BufferPool::markClean(uint32_t page_id) {
    auto it = frames_.find(page_id);
    if (it != frames_.end()) it->second.dirty = false;
}

vector<pair<uint32_t, Page>> BufferPool::getDirtyPages() {
    vector<pair<uint32_t, Page>> result;
    for (auto& kv : frames_) {
        if (kv.second.dirty) result.push_back({kv.first, kv.second.page});
    }
    return result;
}

void BufferPool::touch(uint32_t page_id) {
    lru_list_.remove(page_id);
    lru_list_.push_front(page_id);
}

void BufferPool::evictIfNeeded() {
    if (static_cast<int>(frames_.size()) < capacity_) return;
    for (auto it = lru_list_.rbegin(); it != lru_list_.rend(); ++it) {
        uint32_t victim = *it;
        auto fit = frames_.find(victim);
        if (fit != frames_.end() && fit->second.pin_count == 0) {
            frames_.erase(fit);
            lru_list_.remove(victim);
            return;
        }
    }
}

}  // namespace minidb
