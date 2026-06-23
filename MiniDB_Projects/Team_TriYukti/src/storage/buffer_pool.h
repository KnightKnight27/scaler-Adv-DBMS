#pragma once
#include "storage/page_manager.h"
#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>
#include <vector>

namespace minidb {

class BufferPool {
public:
    BufferPool(size_t pool_size, PageManager *page_manager);
    ~BufferPool();

    Page* FetchPage(page_id_t page_id);
    Page* NewPage(page_id_t *page_id, page_id_t prev_page_id = INVALID_PAGE_ID);
    bool UnpinPage(page_id_t page_id, bool is_dirty);
    bool FlushPage(page_id_t page_id);
    void FlushAllPages();

private:
    struct Frame {
        Page page;
        page_id_t page_id;
        int pin_count;
        bool is_dirty;
        
        Frame() : page_id(INVALID_PAGE_ID), pin_count(0), is_dirty(false) {}
    };

    size_t pool_size_;
    PageManager *page_manager_;
    std::vector<Frame> frames_;
    std::list<size_t> replacer_; // LRU list
    std::unordered_map<page_id_t, size_t> page_table_;
    std::mutex latch_;

    bool FindVictim(size_t *frame_id);
};

} // namespace minidb
