#pragma once

#include <list>
#include <mutex>
#include <unordered_map>

#include "storage/page.h"
#include "storage/page_manager.h"

namespace minidb {

class BufferPool {
public:
    BufferPool(PageManager* page_manager, std::size_t pool_size = DEFAULT_BUFFER_POOL_SIZE);

    Page* FetchPage(int page_id);
    void UnpinPage(int page_id, bool is_dirty);
    void FlushAllPages();
    void ResetCounters();
    std::size_t HitCount() const { return hits_; }
    std::size_t MissCount() const { return misses_; }

private:
    struct Frame {
        int page_id = INVALID_PAGE_ID;
        Page page{0};
        bool dirty = false;
    };

    PageManager* page_manager_;
    std::size_t pool_size_;
    std::vector<Frame> frames_;
    std::unordered_map<int, std::size_t> page_table_;
    std::list<std::size_t> lru_;
    std::mutex mutex_;
    std::size_t hits_ = 0;
    std::size_t misses_ = 0;

    std::size_t EvictFrame();
    void Touch(std::size_t frame_idx);
};

}  // namespace minidb
