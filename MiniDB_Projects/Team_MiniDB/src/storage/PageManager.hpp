#pragma once

#include <fstream>
#include <string>

using namespace std;

#include "storage/BufferPool.hpp"
#include "storage/Page.hpp"

namespace minidb {

class PageManager {
public:
    explicit PageManager(BufferPool& buffer_pool);

    bool open(const string& filepath);
    void close();
    uint32_t allocatePage(PageType type);
    Page* fetchPage(uint32_t page_id);
    void markDirty(uint32_t page_id);
    void flushAll();
    uint32_t getPageCount() const { return page_count_; }

private:
    BufferPool& buffer_pool_;
    fstream file_;
    string filepath_;
    uint32_t page_count_ = 0;

    void extendFile(uint32_t new_page_count);
};

}  // namespace minidb
