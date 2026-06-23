#pragma once

#include <fstream>
#include <mutex>
#include <string>

#include "storage/page.h"

namespace minidb {

class PageManager {
public:
    explicit PageManager(std::string filepath);
    ~PageManager();

    PageManager(const PageManager&) = delete;
    PageManager& operator=(const PageManager&) = delete;

    int AllocatePage();
    Page* FetchPage(int page_id);
    void FlushPage(int page_id, const Page& page);
    void FlushAll();
    int PageCount() const { return page_count_; }

private:
    std::string filepath_;
    std::fstream file_;
    int page_count_ = 0;
    std::mutex mutex_;
};

}  // namespace minidb
