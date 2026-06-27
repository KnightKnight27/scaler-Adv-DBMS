#include "buffer_pool.h"

Page& BufferPool::get_page(HeapFile& file, uint32_t page_id) {
    PageKey key{reinterpret_cast<uintptr_t>(&file), page_id};
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second.page;

    if (cache_.size() >= POOL_MAX) evict_one();

    cache_[key] = CachedPage{&file, file.read_page(page_id)};
    return cache_[key].page;
}

void BufferPool::mark_dirty(HeapFile& file, uint32_t page_id) {
    PageKey key{reinterpret_cast<uintptr_t>(&file), page_id};
    auto it = cache_.find(key);
    if (it != cache_.end()) it->second.page.dirty = true;
}

void BufferPool::flush_all(HeapFile& file) {
    for (auto& [key, cp] : cache_) {
        if (cp.file == &file && cp.page.dirty) {
            cp.file->write_page(cp.page);
            cp.page.dirty = false;
        }
    }
}

void BufferPool::evict_one() {
    auto it = cache_.begin();
    if (it->second.page.dirty) it->second.file->write_page(it->second.page);
    cache_.erase(it);
}
