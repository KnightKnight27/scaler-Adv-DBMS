#pragma once
#include "storage/page.h"
#include <fstream>
#include <string>
#include <mutex>

namespace minidb {

class PageManager {
public:
    PageManager(const std::string &db_file);
    ~PageManager();

    void ReadPage(page_id_t page_id, Page *page);
    void WritePage(page_id_t page_id, const Page *page);
    page_id_t AllocatePage(page_id_t prev_page_id = INVALID_PAGE_ID);

    void Flush();

private:
    std::string db_file_;
    std::fstream db_io_;
    page_id_t next_page_id_;
    std::mutex latch_;
};

} // namespace minidb
