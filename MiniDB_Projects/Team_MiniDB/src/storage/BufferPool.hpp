#pragma once

#include <list>
#include <unordered_map>
#include <vector>

using namespace std;

#include "storage/Page.hpp"

namespace minidb {

class BufferPool {
public:
    explicit BufferPool(int capacity = BUFFER_POOL_CAPACITY);

    Page* getPage(uint32_t page_id);
    void putPage(uint32_t page_id, const Page& page, bool is_dirty);
    void unpinPage(uint32_t page_id);
    void markDirty(uint32_t page_id);
    void markClean(uint32_t page_id);
    vector<pair<uint32_t, Page>> getDirtyPages();

private:
    struct Frame {
        Page page;
        bool dirty = false;
        int pin_count = 0;
    };

    int capacity_;
    unordered_map<uint32_t, Frame> frames_;
    list<uint32_t> lru_list_;

    void touch(uint32_t page_id);
    void evictIfNeeded();
};

}  // namespace minidb
